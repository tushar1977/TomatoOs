
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "idt.h"
#include "util.h"
void keyboardHandler(struct IDTEFrame registers);
void initKeyboard();

void isr_keyboard_handler(struct IDTEFrame *regs);
#endif
