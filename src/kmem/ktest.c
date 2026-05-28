#include "kmem.h"
#include "printf.h"

struct test {
  char i;
  int j;
};

void test() {
  struct test *t = kmalloc(sizeof(*t));

  if (!t) {
    kprintf("malloc failed\n");
    return;
  }

  t->i = 'A';
  t->j = 123;

  kprintf("struct addr: %x\n", t);
  kprintf("i addr: %x\n", &t->i);
  kprintf("j addr: %x\n", &t->j);

  kprintf("i = %c\n", t->i);
  kprintf("j = %d\n", t->j);
}
