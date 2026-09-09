#ifndef FILES_H
#define FILES_H

#include <stdint.h>

#define FILE_PORT 0x0278

#define FILE_OP_OPEN  1
#define FILE_OP_CLOSE 2
#define FILE_OP_READ  3
#define FILE_OP_WRITE 4
#define FILE_OP_LSEEK 5

// guest flags
#define G_O_RD     1
#define G_O_WR     2
#define G_O_RDWR   4
#define G_O_CREATE 8

#define G_SEEK_SET 1
#define G_SEEK_END 2

#define MAX_OPEN_FILES 16
#define MAX_FILENAME 200
#define MAX_RW_SIZE   (1u << 20)  // maksimalna velicina jednog read/write zahtjeva

// prvi fd koji se vraca gostu
#define FD_BASE 3

struct file_entry {
	int  used;
	int  hfd;	// fajl deskriptor na domacinu (host file descriptor)
	int  gflags;	// flagovi sa kojima je gost otvorio fajl
	int  shared_file; // da li je ovo dijeljeni fajl
	char name[MAX_FILENAME + 1];
};

enum file_fsm {
	FS_IDLE,
	FS_OPEN_FLAGS,
	FS_OPEN_NAME,
	FS_CLOSE_FD,
	FS_READ_FD,
	FS_READ_COUNT,
	FS_READ_DATA,
	FS_WRITE_FD,
	FS_WRITE_COUNT,
	FS_WRITE_DATA,
	FS_LSEEK_FD,
	FS_LSEEK_OFF,
	FS_LSEEK_FLAG,
	FS_RESULT
};

struct file_state {
	int vm_id;

	enum file_fsm state;   // trenutno stanje u masini stanja
	enum file_fsm next_state;	// stanje poslije citanja rezultata

	uint32_t op;   // operacija koju je gost poslao (oper, read...)
	uint32_t flags;
	uint32_t fd;   // fajl deskriptor koji je gost poslao
	uint32_t count;
	int32_t  off;   // offset za lseek
	uint32_t whence;

	char name[MAX_FILENAME + 1];
	uint32_t name_len;
	int name_invalid;

	uint8_t *data;      // bafer za read ili write
	uint32_t data_len;
	uint32_t data_pos;
	int discard;        // write preko limita

	int32_t result;

	struct file_entry tab[MAX_OPEN_FILES];
};

void files_set_shared(char **paths, int n);
void files_init(struct file_state *f, int vm_id);
void files_cleanup(struct file_state *f);

int files_io(struct file_state *f, int is_in, int size, void *data);

#endif
