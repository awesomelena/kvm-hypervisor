#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static char **shared_paths;
static int shared_count;

void files_set_shared(char **paths, int n)
{
	shared_paths = paths;
	shared_count = n;
}

static int name_valid(const char *s)
{
	int start_of_part = 1;

	for (const char *p = s; *p; p++) {
		if (start_of_part) {
			// svaka komponenta putanje mora da pocne slovom
			if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')))
				return 0;
			start_of_part = 0;
			continue;
		}

		if (*p == '/') {
			start_of_part = 1;
			continue;
		}

		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9') || *p == '.' || *p == '_')
			continue;
		return 0;
	}

	// putanja ne smije da se zavrsi kosom crtom
	return !start_of_part;
}

// pravi foldere
static void make_dirs(const char *path)
{
	char tmp[256];
	size_t n = strlen(path);

	if (n >= sizeof(tmp))
		return;
	strcpy(tmp, path);

	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
}

static const char *path_basename(const char *path)
{
	const char *p = strrchr(path, '/');
	return p ? p + 1 : path;
}

static void local_path(struct file_state *f, const char *name, char *out, size_t out_sz)
{
	snprintf(out, out_sz, "local_vm%d/%s", f->vm_id, name);
}

void files_init(struct file_state *f, int vm_id)
{
	char dir[64];

	memset(f, 0, sizeof(*f));
	f->vm_id = vm_id;
	f->state = FS_IDLE;

	snprintf(dir, sizeof(dir), "local_vm%d", vm_id);
	mkdir(dir, 0755);
}

void files_cleanup(struct file_state *f)
{
	for (int i = 0; i < MAX_OPEN_FILES; i++) {
		if (f->tab[i].used) {
			close(f->tab[i].hfd);
			f->tab[i].used = 0;
		}
	}
	free(f->data);
	f->data = NULL;
}

static int host_flags(int gflags)
{
	switch (gflags & (G_O_RD | G_O_WR | G_O_RDWR)) {
	case G_O_RD:   return O_RDONLY;
	case G_O_WR:   return O_WRONLY;
	case G_O_RDWR: return O_RDWR;
	default:       return -1;
	}
}

static struct file_entry *get_entry(struct file_state *f, uint32_t gfd)
{
	uint32_t slot = gfd - FD_BASE;

	if (gfd < FD_BASE || slot >= MAX_OPEN_FILES || !f->tab[slot].used)
		return NULL;
	return &f->tab[slot];
}

static int32_t do_open(struct file_state *f)
{
	char lpath[256];
	int hf, slot, shared_file = -1;
	int fd = -1;

	if (f->name_invalid || !name_valid(f->name))
		return -1;

	hf = host_flags((int)f->flags);
	if (hf < 0)
		return -1;

	for (slot = 0; slot < MAX_OPEN_FILES; slot++)
		if (!f->tab[slot].used)
			break;
	if (slot == MAX_OPEN_FILES)
		return -1;

	local_path(f, f->name, lpath, sizeof(lpath));

	if (access(lpath, F_OK) == 0) {
		fd = open(lpath, hf);
	} else {
		for (int k = 0; k < shared_count; k++) {
			if (strcmp(f->name, shared_paths[k]) == 0 ||
			    strcmp(path_basename(f->name), path_basename(shared_paths[k])) == 0) {
				shared_file = k;
				break;
			}
		}

		if (shared_file >= 0) {
			fd = open(shared_paths[shared_file], O_RDONLY);
		} else if (f->flags & G_O_CREATE) {
			make_dirs(lpath);
			fd = open(lpath, hf | O_CREAT, 0644);
		} else {
			return -1;
		}
	}

	if (fd < 0)
		return -1;

	f->tab[slot].used = 1;
	f->tab[slot].hfd = fd;
	f->tab[slot].gflags = (int)f->flags;
	f->tab[slot].shared_file = shared_file;
	strncpy(f->tab[slot].name, f->name, MAX_FILENAME);
	f->tab[slot].name[MAX_FILENAME] = '\0';

	return slot + FD_BASE;
}

static int32_t do_close(struct file_state *f)
{
	struct file_entry *e = get_entry(f, f->fd);

	if (!e)
		return -1;

	close(e->hfd);
	e->used = 0;
	return 0;
}

static int copy_file(const char *src, const char *dst)
{
	char buf[4096];
	ssize_t n;
	int in, out;

	in = open(src, O_RDONLY);
	if (in < 0)
		return -1;

	out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out < 0) {
		close(in);
		return -1;
	}

	while ((n = read(in, buf, sizeof(buf))) > 0) {
		if (write(out, buf, (size_t)n) != n) {
			n = -1;
			break;
		}
	}

	close(in);
	close(out);
	return n < 0 ? -1 : 0;
}

static int cow_if_needed(struct file_state *f, struct file_entry *e)
{
	char lpath[256];
	off_t cur;
	int fd, hf;

	if (e->shared_file < 0)
		return 0;

	cur = lseek(e->hfd, 0, SEEK_CUR);
	if (cur < 0)
		return -1;

	local_path(f, e->name, lpath, sizeof(lpath));
	make_dirs(lpath);
	if (copy_file(shared_paths[e->shared_file], lpath) < 0)
		return -1;

	hf = host_flags(e->gflags);
	fd = open(lpath, hf);
	if (fd < 0)
		return -1;

	if (lseek(fd, cur, SEEK_SET) < 0) {
		close(fd);
		return -1;
	}

	close(e->hfd);
	e->hfd = fd;
	e->shared_file = -1;
	return 0;
}

static int32_t do_read(struct file_state *f)
{
	struct file_entry *e = get_entry(f, f->fd);
	ssize_t n;

	if (!e || !(e->gflags & (G_O_RD | G_O_RDWR)))
		return -1;
	if (f->count == 0)
		return 0;
	if (f->count > MAX_RW_SIZE)
		return -1;

	free(f->data);
	f->data = malloc(f->count);
	if (!f->data)
		return -1;

	n = read(e->hfd, f->data, f->count);
	if (n < 0) {
		free(f->data);
		f->data = NULL;
		return -1;
	}

	f->data_len = (uint32_t)n;
	f->data_pos = 0;
	return (int32_t)n;
}

static int32_t do_write(struct file_state *f)
{
	struct file_entry *e = get_entry(f, f->fd);
	ssize_t n;

	if (!e || !(e->gflags & (G_O_WR | G_O_RDWR)))
		return -1;

	if (cow_if_needed(f, e) < 0)
		return -1;

	if (f->count == 0)
		return 0;

	n = write(e->hfd, f->data, f->count);
	return n < 0 ? -1 : (int32_t)n;
}

static int32_t do_lseek(struct file_state *f)
{
	struct file_entry *e = get_entry(f, f->fd);
	off_t pos;

	if (!e)
		return -1;

	switch (f->whence) {
	case G_SEEK_SET:
		pos = lseek(e->hfd, f->off, SEEK_SET);
		break;
	case G_SEEK_END:
		pos = lseek(e->hfd, 0, SEEK_END);
		break;
	default:
		return -1;
	}

	return pos < 0 ? -1 : (int32_t)pos;
}

// pomocne funkcije za citanje vrijednosti iz kvm_run bafera
static uint32_t get_u32(void *data) { uint32_t v; memcpy(&v, data, 4); return v; }
static uint8_t  get_u8(void *data)  { return *(uint8_t *)data; }

int files_io(struct file_state *f, int is_in, int size, void *data)
{
	if (is_in) {
		switch (f->state) {
		case FS_RESULT:
			if (size != 4)
				return -1;
			memcpy(data, &f->result, 4);
			f->state = f->next_state;
			return 0;
		case FS_READ_DATA:
			if (size != 1)
				return -1;
			*(uint8_t *)data = f->data[f->data_pos++];
			if (f->data_pos == f->data_len) {
				free(f->data);
				f->data = NULL;
				f->state = FS_IDLE;
			}
			return 0;
		default:
			return -1;
		}
	}

	switch (f->state) {
	case FS_IDLE:
		if (size != 4)
			return -1;
		f->op = get_u32(data);
		switch (f->op) {
		case FILE_OP_OPEN:  f->state = FS_OPEN_FLAGS; return 0;
		case FILE_OP_CLOSE: f->state = FS_CLOSE_FD;   return 0;
		case FILE_OP_READ:  f->state = FS_READ_FD;    return 0;
		case FILE_OP_WRITE: f->state = FS_WRITE_FD;   return 0;
		case FILE_OP_LSEEK: f->state = FS_LSEEK_FD;   return 0;
		default:            return -1;
		}

	case FS_OPEN_FLAGS:
		if (size != 4)
			return -1;
		f->flags = get_u32(data);
		f->name_len = 0;
		f->name_invalid = 0;
		f->state = FS_OPEN_NAME;
		return 0;

	case FS_OPEN_NAME: {
		char c;

		if (size != 1)
			return -1;
		c = (char)get_u8(data);
		if (c == '\0') {
			f->name[f->name_len] = '\0';
			f->result = do_open(f);
			f->state = FS_RESULT;
			f->next_state = FS_IDLE;
		} else if (f->name_len < MAX_FILENAME) {
			f->name[f->name_len++] = c;
		} else {
			f->name_invalid = 1;
		}
		return 0;
	}

	case FS_CLOSE_FD:
		if (size != 4)
			return -1;
		f->fd = get_u32(data);
		f->result = do_close(f);
		f->state = FS_RESULT;
		f->next_state = FS_IDLE;
		return 0;

	case FS_READ_FD:
		if (size != 4)
			return -1;
		f->fd = get_u32(data);
		f->state = FS_READ_COUNT;
		return 0;

	case FS_READ_COUNT:
		if (size != 4)
			return -1;
		f->count = get_u32(data);
		f->result = do_read(f);
		f->state = FS_RESULT;
		f->next_state = (f->result > 0) ? FS_READ_DATA : FS_IDLE;
		return 0;

	case FS_WRITE_FD:
		if (size != 4)
			return -1;
		f->fd = get_u32(data);
		f->state = FS_WRITE_COUNT;
		return 0;

	case FS_WRITE_COUNT:
		if (size != 4)
			return -1;
		f->count = get_u32(data);
		f->discard = 0;
		if (f->count == 0) {
			f->result = do_write(f);
			f->state = FS_RESULT;
			f->next_state = FS_IDLE;
			return 0;
		}
		if (f->count > MAX_RW_SIZE) {
			f->discard = 1;
		} else {
			free(f->data);
			f->data = malloc(f->count);
			if (!f->data)
				f->discard = 1;
		}
		f->data_pos = 0;
		f->state = FS_WRITE_DATA;
		return 0;

	case FS_WRITE_DATA:
		if (size != 1)
			return -1;
		if (!f->discard)
			f->data[f->data_pos] = get_u8(data);
		f->data_pos++;
		if (f->data_pos == f->count) {
			f->result = f->discard ? -1 : do_write(f);
			free(f->data);
			f->data = NULL;
			f->state = FS_RESULT;
			f->next_state = FS_IDLE;
		}
		return 0;

	case FS_LSEEK_FD:
		if (size != 4)
			return -1;
		f->fd = get_u32(data);
		f->state = FS_LSEEK_OFF;
		return 0;

	case FS_LSEEK_OFF:
		if (size != 4)
			return -1;
		f->off = (int32_t)get_u32(data);
		f->state = FS_LSEEK_FLAG;
		return 0;

	case FS_LSEEK_FLAG:
		if (size != 4)
			return -1;
		f->whence = get_u32(data);
		f->result = do_lseek(f);
		f->state = FS_RESULT;
		f->next_state = FS_IDLE;
		return 0;

	default:
		return -1;
	}
}
