#ifndef _UNISTD_H_
#define _UNISTD_H_

#include <stddef.h>

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

int access(const char *pathname, int mode);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned long usec);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int isatty(int fd);

#endif
