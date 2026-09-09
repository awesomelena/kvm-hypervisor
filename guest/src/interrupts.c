#include "descriptors.h"
#include "interrupts.h"
#include "io.h"

static struct idt_entry idt[IDT_ENTRIES];

static void (*irq32_handler)(void);

void irq32_set_handler(void (*handler)(void))
{
	irq32_handler = handler;
}

static void __attribute__((interrupt, target("general-regs-only")))
irq32_entry(struct interrupt_frame *frame)
{
	(void)frame;

	if (irq32_handler)
		irq32_handler();
}

static void set_idt_gate(unsigned n, void (*handler)(struct interrupt_frame *))
{
	uint64_t addr = (uint64_t)(uintptr_t)handler;
	idt[n].offset_low  = addr & 0xFFFF;
	idt[n].selector    = 0x08;
	idt[n].ist         = 0;
	idt[n].type_attr   = 0x8E;  // P=1, DPL=0, 64-bit interrupt gate
	idt[n].offset_mid  = (addr >> 16) & 0xFFFF;
	idt[n].offset_high = (addr >> 32) & 0xFFFFFFFF;
	idt[n].reserved    = 0;
}

void init_idt(void)
{
	struct dt_ptr p;

	set_idt_gate(32, irq32_entry);

	p.limit = sizeof(idt) - 1;
	p.base  = (uint64_t)(uintptr_t)idt;
	asm volatile("lidt %0" : : "m"(p) : "memory");
}
