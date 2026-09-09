#ifndef LIB_H
#define LIB_H

void guest_init(void);

void gdt_boot(void);
void halt_forever(void) __attribute__((noreturn));

void print_str(const char *s);
void print_n(const char *s, int n);
void print_num(long x);

#endif
