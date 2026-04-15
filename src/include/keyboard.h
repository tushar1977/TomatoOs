
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "util.h"
__attribute__((interrupt)) void keyboardhandler(struct IDTEFrame *frame);
void initKeyboard();

#endif
