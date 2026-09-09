#include "file.h"
#include "interrupts.h"
#include "io.h"
#include "lib.h"

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void)
{
	char buf[128];
	int fd, n;

	guest_init();

	asm volatile("sti");

	fd = open("deljeni.txt", O_WR);
	if (fd < 0) {
		print_str("[B2] greska pri otvaranju deljeni.txt\n");
	} else {
		n = write(fd, "vmc hello\n", 10);
		print_str("[B2] u deljeni.txt (lokalnu kopiju) upisano ");
		print_num(n);
		print_str(" bajtova\n");
		close(fd);
	}

	fd = open("deljeni.txt", O_RD);
	if (fd < 0) {
		print_str("[B2] greska pri ponovnom otvaranju deljeni.txt\n");
	} else {
		n = read(fd, buf, sizeof(buf));
		print_str("[B2] deljeni.txt poslije upisa: ");
		if (n > 0)
			print_n(buf, n);
		close(fd);
	}

	fd = open("privatni.txt", O_WR | O_CREATE);
	if (fd >= 0) {
		write(fd, "hello world\n", 12);
		close(fd);
	}

	fd = open("privatni.txt", O_RD);
	if (fd >= 0) {
		n = read(fd, buf, sizeof(buf));
		print_str("[B2] privatni.txt: ");
		if (n > 0)
			print_n(buf, n);
		close(fd);
	}

	print_str("[B2] kraj\n");

	for (;;)
		asm volatile("hlt");
}
