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

	fd = open("deljeni.txt", O_RD);
	if (fd < 0) {
		print_str("[B1] greska pri otvaranju deljeni.txt\n");
	} else {
		n = read(fd, buf, sizeof(buf));
		print_str("[B1] deljeni.txt: ");
		if (n > 0)
			print_n(buf, n);
		close(fd);
	}

	fd = open("privatni.txt", O_WR | O_CREATE);
	if (fd < 0) {
		print_str("[B1] greska pri otvaranju privatni.txt\n");
	} else {
		n = write(fd, "hello world\n", 12);
		print_str("[B1] u privatni.txt upisano ");
		print_num(n);
		print_str(" bajtova\n");

		n = lseek(fd, 0, SEEK_END);
		print_str("[B1] velicina privatni.txt: ");
		print_num(n);
		print_str(" bajtova\n");
		close(fd);
	}

	fd = open("privatni.txt", O_RD);
	if (fd < 0) {
		print_str("[B1] greska pri ponovnom otvaranju privatni.txt\n");
	} else {
		n = read(fd, buf, sizeof(buf));
		print_str("[B1] privatni.txt: ");
		if (n > 0)
			print_n(buf, n);

		lseek(fd, 6, SEEK_SET);
		n = read(fd, buf, 5);
		print_str("[B1] privatni.txt od pozicije 6: ");
		print_n(buf, n);
		print_str("\n");
		close(fd);
	}

	print_str("[B1] kraj\n");

	for (;;)
		asm volatile("hlt");
}
