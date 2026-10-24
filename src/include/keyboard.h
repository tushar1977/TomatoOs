#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "util.h"

struct command {
  const char *name;
  const char *help;
  void (*fn)(int argc, char **argv);
};
__attribute__((interrupt)) void keyboardhandler(struct IDTEFrame *frame);
void initKeyboard(void);
void keyboard_poll(void); // keep polling from keyboard buffer for scancodes
void keyboard_idle(void);

void shell_run(void);
#endif
