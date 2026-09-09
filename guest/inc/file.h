#ifndef FILE_H
#define FILE_H

#define FILE_PORT 0x0278

#define FILE_OP_OPEN  1
#define FILE_OP_CLOSE 2
#define FILE_OP_READ  3
#define FILE_OP_WRITE 4
#define FILE_OP_LSEEK 5

#define O_RD     1
#define O_WR     2
#define O_RDWR   4
#define O_CREATE 8

#define SEEK_SET 1
#define SEEK_END 2

int open(const char *path, int flags);
int close(int fd);
int read(int fd, char *buf, int count);
int write(int fd, const char *buf, int count);
int lseek(int fd, const int offset, int off_flag);

#endif
