#ifndef USER_UNISTD_H
#define USER_UNISTD_H

long write(int fd, const void *buf, unsigned long n);
long read(int fd, void *buf, unsigned long n);
void exit(int status);
long spawn(const char *path);
long waitpid(void);
long getpid(void);
long yield(void);

struct dirent {
    char name[100];
    unsigned int ino;
    unsigned int mode;
};
#define S_IFDIR 0x4000u

long open(const char *path, int flags);
long close(int fd);
long readdir(int fd, struct dirent *ent);

#endif
