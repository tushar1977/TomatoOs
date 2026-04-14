
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "idt.h"
#include "util.h"
__attribute__((interrupt)) void keyboardhandler(struct IDTEFrame *frame);
void initKeyboard();

void isr_keyboard_handler(struct IDTEFrame *regs);
#endif
