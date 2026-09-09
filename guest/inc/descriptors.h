#ifndef DESCRIPTORS_H
#define DESCRIPTORS_H

#include <stdint.h>

#define IDT_ENTRIES 64

struct interrupt_frame {
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
};

struct idt_entry {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t  ist;
	uint8_t  type_attr;     // P | DPL(2) | 0 | type(4)
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t reserved;
} __attribute__((packed));

struct dt_ptr {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

struct gdt_entry {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t  base_mid;
	uint8_t  access;       // P | DPL(2) | S | type(4)
	uint8_t  flags_limit;  // G | D/B | L | AVL in high nibble; limit[19:16] in low nibble
	uint8_t  base_high;
} __attribute__((packed));

#endif
