#ifndef ARGS_H
#define ARGS_H

#include <stddef.h>  // zbog size_t

#include "vm.h"  // zbog MAX_VMS

struct args {
	size_t mem_size;
	size_t page_size;

	char *guest_images[MAX_VMS];
	int guest_count;

	char *shared_files[MAX_VMS];
	int shared_file_count;
};

int parse_args(int argc, char *argv[], struct args *cfg);

void print_usage(const char *prog);

#endif
