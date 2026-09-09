#include "vm.h"
#include "files.h"
#include "buffer.h"
#include "args.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>

static struct args cfg;

struct vm_ctx {
	int id;
	const char *image;
	struct file_state files;
};

static int handle_io(struct vm_ctx *ctx, int is_in, uint16_t port, int size, void *data)
{
	switch (port) {
	case 0xE9:
		if (size != 1)
			return -1;
		if (is_in) {
			int c = getchar();
			*(uint8_t *)data = (c == EOF) ? 0 : (uint8_t)c;
		} else {
			putchar(*(uint8_t *)data);
			fflush(stdout);
		}
		return 0;

	case FILE_PORT:
		return files_io(&ctx->files, is_in, size, data);

	case BUFFER_PORT_DATA:
	case BUFFER_PORT_ACK:
		return buffer_io(ctx->id, is_in, size, port, data);

	case BUFFER_PORT_POLL:
		if (!is_in)
			return -1;
		memset(data, 0, (size_t)size);
		return 0;

	default:
		return -1;
	}
}

static void *vm_thread(void *arg)
{
	struct vm_ctx *ctx = arg;
	struct vm v;
	struct kvm_sregs sregs;
	struct kvm_regs regs;
	int stop = 0;
	int failed = 0;

	files_init(&ctx->files, ctx->id);

	if (vm_init(&v, cfg.mem_size)) {
		printf("[VM %d] Neuspijesna inicijalizacija VM\n", ctx->id);
		goto out;
	}

	if (ioctl(v.vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		goto out;
	}

	setup_long_mode(&v, &sregs, cfg.page_size);

	if (ioctl(v.vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		goto out;
	}

	if (load_guest_image(&v, ctx->image, GUEST_START_ADDR) < 0) {
		printf("[VM %d] Neuspijesno ucitavanje slike gosta '%s'\n", ctx->id, ctx->image);
		goto out;
	}

	memset(&regs, 0, sizeof(regs));
	regs.rflags = 0x2;
	regs.rip = GUEST_START_ADDR;
	regs.rsp = cfg.mem_size;

	if (ioctl(v.vcpu_fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		goto out;
	}

	while (stop == 0) {
		v.run->request_interrupt_window = buffer_irq_pending(ctx->id) ? 1 : 0;

		if (ioctl(v.vcpu_fd, KVM_RUN, 0) == -1) {
			printf("[VM %d] KVM_RUN nije uspio\n", ctx->id);
			failed = 1;
			break;
		}

		switch (v.run->exit_reason) {
		case KVM_EXIT_IO: {
			uint8_t *p = (uint8_t *)v.run + v.run->io.data_offset;
			int ret = 0;

			for (uint32_t i = 0; i < v.run->io.count && ret == 0; i++) {
				ret = handle_io(ctx, v.run->io.direction == KVM_EXIT_IO_IN,
						v.run->io.port, v.run->io.size, p);
				p += v.run->io.size;
			}
			if (ret < 0) {
				printf("[VM %d] Neocekivani IO pristup (port 0x%x, %s, %d B) - zaustavlja se VM\n",
				       ctx->id, v.run->io.port,
				       v.run->io.direction == KVM_EXIT_IO_IN ? "in" : "out",
				       v.run->io.size);
				failed = 1;
				stop = 1;
			}
			continue;
		}
		case KVM_EXIT_IRQ_WINDOW_OPEN:
			if (buffer_irq_take(ctx->id)) {
				if (inject_irq(&v, IRQ_NUM) < 0) {
					failed = 1;
					stop = 1;
				}
			}
			continue;
		case KVM_EXIT_HLT:
			printf("[VM %d] KVM_EXIT_HLT\n", ctx->id);
			stop = 1;
			break;
		default:
			printf("[VM %d] Neocekivani VM izlaz - kod greske: %d\n",
			       ctx->id, v.run->exit_reason);
			failed = 1;
			stop = 1;
			break;
		}
	}

out:
	vm_destroy(&v);
	buffer_vm_died(ctx->id);
	files_cleanup(&ctx->files);
	return failed ? (void *)1 : NULL;
}

int main(int argc, char *argv[])
{
	static struct vm_ctx ctx[MAX_VMS];
	pthread_t tid[MAX_VMS];
	int created[MAX_VMS] = { 0 };

	if (parse_args(argc, argv, &cfg) < 0) {
		print_usage(argv[0]);
		return 1;
	}

	files_set_shared(cfg.shared_files, cfg.shared_file_count);
	buffer_init(cfg.guest_count);

	for (int i = 0; i < cfg.guest_count; i++) {
		ctx[i].id = i;
		ctx[i].image = cfg.guest_images[i];
		if (pthread_create(&tid[i], NULL, vm_thread, &ctx[i]) != 0) {
			perror("pthread_create");
			buffer_vm_died(i);
		} else {
			created[i] = 1;
		}
	}

	for (int i = 0; i < cfg.guest_count; i++)
		if (created[i])
			pthread_join(tid[i], NULL);

	return 0;
}
