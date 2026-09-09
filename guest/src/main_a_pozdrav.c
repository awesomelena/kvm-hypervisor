#include "interrupts.h"
#include "io.h"
#include "lib.h"

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void)
{
	guest_init();

	asm volatile("sti");

	print_str("Pozdrav iz faze A!\n");

	for (;;)
		asm volatile("hlt");
}
