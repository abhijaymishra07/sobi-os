#ifndef USER_UNISTD_H
#define USER_UNISTD_H

long write(int fd, const void *buf, unsigned long n);
long read(int fd, void *buf, unsigned long n);
void exit(int status);

#endif
