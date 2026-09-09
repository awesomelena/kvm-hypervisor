#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>

#define BUFFER_PORT_DATA 0x510
#define BUFFER_PORT_POLL 0x511
#define BUFFER_PORT_ACK  0x520

#define BUFFER_SIZE 1024

void buffer_init(int n_vms);

int buffer_io(int vm_id, int is_in, int size, uint16_t port, void *data);

// upravljenje prekidima
int buffer_irq_pending(int vm_id);
int buffer_irq_take(int vm_id);

// sprjecava deadlock  
void buffer_vm_died(int vm_id);

#endif
