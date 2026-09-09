#include "file.h"
#include "interrupts.h"
#include "io.h"
#include "lib.h"

static int open_report(const char *name, int flags)
{
	int fd = open(name, flags);

	print_str("  open(\"");
	print_str(name);
	print_str("\") -> ");
	print_num(fd);
	print_str("\n");
	return fd;
}

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void)
{
	char buf[128];
	int fd, n;

	guest_init();
	asm volatile("sti");

	print_str("[B] nevalidna imena (ocekivano -1):\n");
	open_report("1data.txt", O_WR | O_CREATE);  // ne pocinje slovom
	open_report("da-ta.txt", O_WR | O_CREATE);  // nedozvoljen znak '-'
	open_report("nemaovoga.txt", O_RD);         // ne postoji, nema O_CREATE

	print_str("[B] lokalni fajl lokalni.txt:\n");
	fd = open_report("lokalni.txt", O_WR | O_CREATE);
	if (fd >= 0) {
		n = write(fd, "podatak u lokalnom fajlu\n", 25);
		print_str("  upisano ");
		print_num(n);
		print_str(" bajtova\n");
		close(fd);
	}

	fd = open("lokalni.txt", O_RD);
	if (fd >= 0) {
		n = read(fd, buf, sizeof(buf));
		print_str("  procitano nazad: ");
		if (n > 0)
			print_n(buf, n);
		close(fd);
	}

	print_str("[B] dijeljeni fajl deljeni.txt:\n");
	fd = open("deljeni.txt", O_RD);
	if (fd >= 0) {
		n = read(fd, buf, sizeof(buf));
		print_str("  prije upisa: ");
		if (n > 0)
			print_n(buf, n);
		close(fd);
	}

	fd = open("deljeni.txt", O_WR);
	if (fd >= 0) {
		n = write(fd, "COW!", 4);
		print_str("  upisano ");
		print_num(n);
		print_str(" bajtova (pokrenut copy-on-write)\n");
		close(fd);
	}

	fd = open("deljeni.txt", O_RD);
	if (fd >= 0) {
		n = read(fd, buf, sizeof(buf));
		print_str("  poslije upisa (lokalna kopija): ");
		if (n > 0)
			print_n(buf, n);
		close(fd);
	}

	print_str("[B] kraj - original deljeni.txt je nepromijenjen\n");

	for (;;)
		asm volatile("hlt");
}
