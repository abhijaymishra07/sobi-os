#ifndef SOBI_LOG_H
#define SOBI_LOG_H

#include <sobi/types.h>
#include <sobi/compiler.h>

enum log_level {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL,
};

void log_set_level(enum log_level level);
void klog(enum log_level level, const char *subsys, const char *fmt, ...) __printf(3, 4);

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);
int kprintf(const char *fmt, ...) __printf(1, 2);
int kvprintf(const char *fmt, va_list ap);

#define pr_debug(sub, ...) klog(LOG_DEBUG, sub, __VA_ARGS__)
#define pr_info(sub, ...) klog(LOG_INFO, sub, __VA_ARGS__)
#define pr_warn(sub, ...) klog(LOG_WARN, sub, __VA_ARGS__)
#define pr_err(sub, ...) klog(LOG_ERROR, sub, __VA_ARGS__)

#endif
