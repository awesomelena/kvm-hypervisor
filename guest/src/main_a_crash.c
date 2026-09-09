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

	print_str("Guest namerno izaziva izuzetak (ud2)...\n");

	asm volatile("ud2");

	for (;;)
		asm volatile("hlt");
}
