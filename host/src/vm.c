#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

int vm_init(struct vm *v, size_t mem_size)
{
	struct kvm_userspace_memory_region region;

	memset(v, 0, sizeof(*v));
	v->kvm_fd = v->vm_fd = v->vcpu_fd = -1;
	v->mem = MAP_FAILED;
	v->run = MAP_FAILED;
	v->run_mmap_size = 0;
	v->mem_size = mem_size;

	v->kvm_fd = open("/dev/kvm", O_RDWR);
	if (v->kvm_fd < 0) {
		perror("open /dev/kvm");
		return -1;
	}

	int api = ioctl(v->kvm_fd, KVM_GET_API_VERSION, 0);
	if (api != KVM_API_VERSION) {
		printf("KVM API mismatch: kernel=%d headers=%d\n", api, KVM_API_VERSION);
		return -1;
	}

	v->vm_fd = ioctl(v->kvm_fd, KVM_CREATE_VM, 0);
	if (v->vm_fd < 0) {
		perror("KVM_CREATE_VM");
		return -1;
	}

	v->mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (v->mem == MAP_FAILED) {
		perror("mmap mem");
		return -1;
	}

	region.slot = 0;
	region.flags = 0;
	region.guest_phys_addr = 0;
	region.memory_size = v->mem_size;
	region.userspace_addr = (uintptr_t)v->mem;
	if (ioctl(v->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
		perror("KVM_SET_USER_MEMORY_REGION");
		return -1;
	}

	v->vcpu_fd = ioctl(v->vm_fd, KVM_CREATE_VCPU, 0);
	if (v->vcpu_fd < 0) {
		perror("KVM_CREATE_VCPU");
		return -1;
	}

	v->run_mmap_size = ioctl(v->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (v->run_mmap_size <= 0) {
		perror("KVM_GET_VCPU_MMAP_SIZE");
		return -1;
	}

	v->run = mmap(NULL, v->run_mmap_size, PROT_READ | PROT_WRITE,
		      MAP_SHARED, v->vcpu_fd, 0);
	if (v->run == MAP_FAILED) {
		perror("mmap kvm_run");
		return -1;
	}

	return 0;
}

void vm_destroy(struct vm *v)
{
	if (v->run && v->run != MAP_FAILED) {
		munmap(v->run, (size_t)v->run_mmap_size);
		v->run = MAP_FAILED;
	}

	if (v->mem && v->mem != MAP_FAILED) {
		munmap(v->mem, v->mem_size);
		v->mem = MAP_FAILED;
	}

	if (v->vcpu_fd >= 0) {
		close(v->vcpu_fd);
		v->vcpu_fd = -1;
	}

	if (v->vm_fd >= 0) {
		close(v->vm_fd);
		v->vm_fd = -1;
	}

	if (v->kvm_fd >= 0) {
		close(v->kvm_fd);
		v->kvm_fd = -1;
	}
}

static void setup_segments_64(struct kvm_sregs *sregs)
{
	struct kvm_segment code = {
		.base    = 0,
		.limit   = 0xffffffff,
		.present = 1,
		.type    = 11,
		.dpl     = 0,
		.db      = 0,
		.s       = 1,
		.l       = 1,
		.g       = 1,
	};
	struct kvm_segment data = code;
	data.type = 3;
	data.l    = 0;

	sregs->cs = code;
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = data;
}

void setup_long_mode(struct vm *v, struct kvm_sregs *sregs, size_t page_size)
{
	uint64_t pml4_addr = 0x1000;
	uint64_t *pml4 = (void *)(v->mem + pml4_addr);

	uint64_t pdpt_addr = 0x2000;
	uint64_t *pdpt = (void *)(v->mem + pdpt_addr);

	uint64_t pd_addr = 0x3000;
	uint64_t *pd = (void *)(v->mem + pd_addr);

	uint64_t pt_base = 0x4000;

	pml4[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pdpt_addr;
	pdpt[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pd_addr;

	if (page_size == PAGE_SIZE_2MB) {
		size_t n_pd = v->mem_size / PAGE_SIZE_2MB;
		for (size_t i = 0; i < n_pd; i++)
			pd[i] = (i * PAGE_SIZE_2MB) | PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;
	} else {
		size_t n_pt = v->mem_size / PAGE_SIZE_2MB;
		for (size_t t = 0; t < n_pt; t++) {
			uint64_t pt_addr = pt_base + t * 0x1000;
			uint64_t *pt = (void *)(v->mem + pt_addr);

			pd[t] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pt_addr;
			for (size_t i = 0; i < 512; i++)
				pt[i] = ((t * 512 + i) * PAGE_SIZE_4KB) | PDE64_PRESENT | PDE64_RW | PDE64_USER;
		}
	}

	sregs->cr3  = pml4_addr;
	sregs->cr4  = CR4_PAE;
	sregs->cr0  = CR0_PE | CR0_PG;
	sregs->efer = EFER_LME | EFER_LMA;

	setup_segments_64(sregs);
}

int load_guest_image(struct vm *v, const char *image_path, uint64_t load_addr)
{
	FILE *f = fopen(image_path, "rb");
	if (!f) {
		perror("Neuspijesno otvaranje slike gosta");
		return -1;
	}

	if (fseek(f, 0, SEEK_END) < 0) {
		perror("Neuspijesno pozicioniranje na kraj gost slike");
		fclose(f);
		return -1;
	}

	long fsz = ftell(f);
	if (fsz < 0) {
		perror("Neuspijesno citanje velicine slike gosta");
		fclose(f);
		return -1;
	}
	rewind(f);

	if ((uint64_t)fsz > v->mem_size - load_addr) {
		printf("Gost slika je prevelika za memoriju VM\n");
		fclose(f);
		return -1;
	}

	if (fread((uint8_t *)v->mem + load_addr, 1, (size_t)fsz, f) != (size_t)fsz) {
		perror("Neuspijesno citanje gost slike");
		fclose(f);
		return -1;
	}
	fclose(f);

	return 0;
}

int inject_irq(struct vm *v, unsigned int vector)
{
	struct kvm_interrupt irq = { .irq = vector };

	if (ioctl(v->vcpu_fd, KVM_INTERRUPT, &irq) < 0) {
		perror("KVM_INTERRUPT");
		return -1;
	}
	return 0;
}
