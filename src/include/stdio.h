#ifndef _STDIO_H
#define _STDIO_H 1

#include "cdefs.h"
#include <stdarg.h>

#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

// colours
int printf(const char *__restrict, ...);
int putchar(int);
int puts(const char *);
#ifdef __cplusplus
}
#endif

#endif
