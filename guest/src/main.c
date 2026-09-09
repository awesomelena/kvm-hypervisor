#include "file.h"
#include "interrupts.h"
#include "io.h"
#include "lib.h"

#define DATA_PORT 0x510
#define POLL_PORT 0x511
#define ACK_PORT  0x520

#define BUFFER_SIZE 1024  // mora da se poklapa sa BUFFER_SIZE hipervizora
#define CHUNK 64

static volatile int role = -1;
static volatile int role_known = 0;
static volatile int done = 0;

static int in_fd = -1;
static int out_fd = -1;

static uint8_t data_buf[BUFFER_SIZE];

static void v_handler(void)
{
	if (!role_known) {
		role = (int)inl(DATA_PORT);
		role_known = 1;
		return;
	}

	if (done)
		return;

	if (role == 1) {
		char chunk[CHUNK];
		int n = 0;

		if (in_fd >= 0)
			n = read(in_fd, chunk, CHUNK);
		if (n < 0)
			n = 0;

		outl(DATA_PORT, (uint32_t)n);
		for (int i = 0; i < n; i++)
			outb(DATA_PORT, (uint8_t)chunk[i]);

		inl(ACK_PORT);

		if (n == 0)
			done = 1;
	} else {
		uint32_t len = inl(DATA_PORT);

		for (uint32_t i = 0; i < len && i < BUFFER_SIZE; i++)
			data_buf[i] = inb(DATA_PORT);

		outl(ACK_PORT, len);

		if (len == 0) {
			done = 1;
			return;
		}

		if (out_fd >= 0)
			write(out_fd, (const char *)data_buf, (int)len);
	}
}

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void)
{
	guest_init();
	irq32_set_handler(v_handler);

	in_fd  = open("ulaz.txt", O_RD);
	out_fd = open("izlaz.txt", O_WR | O_CREATE);

	asm volatile("sti");

	while (!done)
		inb(POLL_PORT);

	asm volatile("cli");

	if (in_fd >= 0)
		close(in_fd);
	if (out_fd >= 0)
		close(out_fd);

	if (role == 1)
		print_str("[V] pisac zavrsio prenos\n");
	else
		print_str("[V] citalac zavrsio prenos\n");

	for (;;)
		asm volatile("hlt");
}
