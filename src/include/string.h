#ifndef _STRING_H
#define _STRING_H 1

#include "cdefs.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int strcmp(const char *s1, const char *s2);
int memcmp(const void *, const void *, size_t);
void *memcpy(void *__restrict, const void *__restrict, size_t);
void *memmove(void *, const void *, size_t);

void *memset(void *bufptr, int value, size_t size);
char *memchr2(const void *str, char ch);
size_t strlen(const char *);

void strncpy(char *_dst, const char *_src, size_t _n);
#ifdef __cplusplus
}
#endif

#endif
