#include "interrupts.h"
#include "io.h"
#include "lib.h"

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void)
{
	char line[128];
	int n = 0;

	guest_init();

	asm volatile("sti");

	print_str("Unesite tekst: ");

	while (n < (int)sizeof(line)) {
		char c = (char)inb(0xE9);
		if (c == '\0' || c == '\n')
			break;
		line[n++] = c;
	}

	print_str("Procitano sa serijskog porta: ");
	print_n(line, n);
	print_str("\n");

	for (;;)
		asm volatile("hlt");
}
