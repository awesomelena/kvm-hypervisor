#ifndef VM_H
#define VM_H

#include <stddef.h>
#include <stdint.h>
#include <linux/kvm.h>

#define GUEST_START_ADDR 0x8000

#define MAX_VMS 16	// max vm-ova koji mogu da idu u isto vrijeme

#define IRQ_NUM 32	// broj prekida

#define PAGE_SIZE_4KB (4u * 1024u)
#define PAGE_SIZE_2MB (2u * 1024u * 1024u)

// pde - page directory entry
#define PDE64_PRESENT (1u << 0)
#define PDE64_RW      (1u << 1)	 // read/write memorija
#define PDE64_USER    (1u << 2)
#define PDE64_PS      (1u << 7)

// kontrolni registri
#define CR0_PE   (1u << 0)  // protected mode
#define CR0_PG   (1u << 31) // paging
#define CR4_PAE  (1u << 5)  // physical address extension
#define EFER_LME (1u << 8) // long mode enable
#define EFER_LMA (1u << 10) // long mode active

struct vm {
	int kvm_fd;
	int vm_fd;   // deskriptor za vm
	int vcpu_fd; // deskriptor za virtuelni cpu
	char *mem;
	size_t mem_size;
	struct kvm_run *run;
	int run_mmap_size;
};

int  vm_init(struct vm *v, size_t mem_size);
void vm_destroy(struct vm *v);
void setup_long_mode(struct vm *v, struct kvm_sregs *sregs, size_t page_size);
int  load_guest_image(struct vm *v, const char *image_path, uint64_t load_addr);
int  inject_irq(struct vm *v, unsigned int vector);

#endif
