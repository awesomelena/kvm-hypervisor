#include "descriptors.h"
#include "interrupts.h"
#include "io.h"
#include "lib.h"

static struct gdt_entry gdt[3];

void guest_init(void)
{
	struct dt_ptr p;

	gdt[0] = (struct gdt_entry){ 0 };
	gdt[1] = (struct gdt_entry){  // 64-bit code, selector 0x08: P=1, DPL=0, S=1, type=0xA, L=1, G=1
		.limit_low   = 0xFFFF,
		.access      = 0x9A,
		.flags_limit = 0xAF,
	};
	gdt[2] = (struct gdt_entry){  // 64-bit data, selector 0x10: P=1, DPL=0, S=1, type=0x2, D/B=1, G=1
		.limit_low   = 0xFFFF,
		.access      = 0x92,
		.flags_limit = 0xCF,
	};

	p.limit = sizeof(gdt) - 1;
	p.base  = (uint64_t)(uintptr_t)gdt;
	asm volatile("lgdt %0" : : "m"(p) : "memory");

	asm volatile(
		"pushq $0x08\n\t"
		"lea 1f(%%rip), %%rax\n\t"
		"pushq %%rax\n\t"
		"lretq\n\t"
		"1:\n\t"
		::: "rax", "memory"
	);

	asm volatile(
		"movl $0x10, %%eax\n\t"
		"movw %%ax, %%ds\n\t"
		"movw %%ax, %%es\n\t"
		"movw %%ax, %%ss\n\t"
		::: "eax", "memory"
	);

	init_idt();
}

void gdt_boot(void)
{
	guest_init();
}

void halt_forever(void)
{
	for (;;)
		asm volatile("hlt");
}

void print_str(const char *s)
{
	for (; *s; s++)
		outb(0xE9, (uint8_t)*s);
}

void print_n(const char *s, int n)
{
	for (int i = 0; i < n; i++)
		outb(0xE9, (uint8_t)s[i]);
}

void print_num(long x)
{
	char tmp[24];
	int i = 0;

	if (x < 0) {
		outb(0xE9, '-');
		x = -x;
	}
	if (x == 0) {
		outb(0xE9, '0');
		return;
	}
	while (x > 0) {
		tmp[i++] = (char)('0' + x % 10);
		x /= 10;
	}
	while (i > 0)
		outb(0xE9, (uint8_t)tmp[--i]);
}
