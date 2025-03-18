#include "../include/string.h"
void strncpy(char *_dst, const char *_src, size_t _n) {
  size_t i = 0;
  while (i++ != _n && (*_dst++ = *_src++))
    ;
}
