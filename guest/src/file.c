#include "file.h"
#include "io.h"

int open(const char *path, int flags)
{
	outl(FILE_PORT, FILE_OP_OPEN);
	outl(FILE_PORT, (uint32_t)flags);

	// ime se salje bajt po bajt ukljucujuci i '\0'
	do {
		outb(FILE_PORT, (uint8_t)*path);
	} while (*path++);

	return (int)inl(FILE_PORT);
}

int close(int fd)
{
	outl(FILE_PORT, FILE_OP_CLOSE);
	outl(FILE_PORT, (uint32_t)fd);

	return (int)inl(FILE_PORT);
}

int read(int fd, char *buf, int count)
{
	int n;

	outl(FILE_PORT, FILE_OP_READ);
	outl(FILE_PORT, (uint32_t)fd);
	outl(FILE_PORT, (uint32_t)count);

	n = (int)inl(FILE_PORT);
	for (int i = 0; i < n; i++)
		buf[i] = (char)inb(FILE_PORT);

	return n;
}

int write(int fd, const char *buf, int count)
{
	outl(FILE_PORT, FILE_OP_WRITE);
	outl(FILE_PORT, (uint32_t)fd);
	outl(FILE_PORT, (uint32_t)count);

	for (int i = 0; i < count; i++)
		outb(FILE_PORT, (uint8_t)buf[i]);

	return (int)inl(FILE_PORT);
}

int lseek(int fd, const int offset, int off_flag)
{
	outl(FILE_PORT, FILE_OP_LSEEK);
	outl(FILE_PORT, (uint32_t)fd);
	outl(FILE_PORT, (uint32_t)offset);
	outl(FILE_PORT, (uint32_t)off_flag);

	return (int)inl(FILE_PORT);
}
