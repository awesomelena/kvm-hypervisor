#ifndef INTERRUPTS_H
#define INTERRUPTS_H

void init_idt(void);

void irq32_set_handler(void (*handler)(void));

#endif
