#include "keyboard.h"
#include "apic.h"
#include "apic_timer.h"
#include "fb.h"
#include "idt.h"
#include "kernel.h"
#include "kmem.h"
#include "memory.h"
#include "power.h"
#include "printf.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"
#include "util.h"
#include "vfs.h"
#include <stdbool.h>

enum {
  UNKNOWN = 0x100,
  ESC,
  CTRL,
  LSHFT,
  RSHFT,
  ALT,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  SCRLCK,
  HOME,
  UP,
  LEFT,
  RIGHT,
  DOWN,
  PGUP,
  PGDOWN,
  END,
  INS,
  DEL,
  CAPS,
  NONE,
  ALTGR,
  NUMLCK
};

static const uint32_t lowercase[128] = {

    UNKNOWN, ESC,     '1',     '2',     '3',     '4',     '5',     '6',
    '7',     '8',     '9',     '0',     '-',     '=',     '\b',    '\t',
    'q',     'w',     'e',     'r',     't',     'y',     'u',     'i',
    'o',     'p',     '[',     ']',     '\n',    CTRL,    'a',     's',
    'd',     'f',     'g',     'h',     'j',     'k',     'l',     ';',
    '\'',    '`',     LSHFT,   '\\',    'z',     'x',     'c',     'v',
    'b',     'n',     'm',     ',',     '.',     '/',     RSHFT,   '*',
    ALT,     ' ',     CAPS,    F1,      F2,      F3,      F4,      F5,
    F6,      F7,      F8,      F9,      F10,     NUMLCK,  SCRLCK,  HOME,
    UP,      PGUP,    '-',     LEFT,    UNKNOWN, RIGHT,   '+',     END,
    DOWN,    PGDOWN,  INS,     DEL,     UNKNOWN, UNKNOWN, UNKNOWN, F11,
    F12,     UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN};

static const uint32_t uppercase[128] = {
    UNKNOWN, ESC,     '!',     '@',     '#',     '$',     '%',     '^',
    '&',     '*',     '(',     ')',     '_',     '+',     '\b',    '\t',
    'Q',     'W',     'E',     'R',     'T',     'Y',     'U',     'I',
    'O',     'P',     '{',     '}',     '\n',    CTRL,    'A',     'S',
    'D',     'F',     'G',     'H',     'J',     'K',     'L',     ':',
    '"',     '~',     LSHFT,   '|',     'Z',     'X',     'C',     'V',
    'B',     'N',     'M',     '<',     '>',     '?',     RSHFT,   '*',
    ALT,     ' ',     CAPS,    F1,      F2,      F3,      F4,      F5,
    F6,      F7,      F8,      F9,      F10,     NUMLCK,  SCRLCK,  HOME,
    UP,      PGUP,    '-',     LEFT,    UNKNOWN, RIGHT,   '+',     END,
    DOWN,    PGDOWN,  INS,     DEL,     UNKNOWN, UNKNOWN, UNKNOWN, F11,
    F12,     UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN};

#define KBD_BUF 128
static volatile uint8_t kbd_buf[KBD_BUF];
static volatile uint32_t kbd_head, kbd_tail;

static bool shift_down, caps_lock;
static char line[100];
static size_t line_len;

char *slice(const char *str, size_t start, size_t end) {
  size_t len = end - start;
  char *result = (char *)kmalloc(len + 1);
  if (!result) {
    return NULL;
  }
  memcpy(result, str + start, len);
  result[len] = '\0';
  return result;
}

static void cmd_clear(int c, char **v) {
  (void)c;
  (void)v;
  clear();
}
static void cmd_reboot(int c, char **v) {
  (void)c;
  (void)v;
  reboot();
}
static void cmd_shutdown(int c, char **v) {
  (void)c;
  (void)v;
  shutdown();
}
static void cmd_ls(int c, char **v) {
  (void)c;
  (void)v;
  display_all_files();
}

static void cmd_help(int c, char **v);

static const struct command commands[] = {
    {"clear", "clear the screen", cmd_clear},
    {"reboot", "restart the machine", cmd_reboot},
    {"shutdown", "power off", cmd_shutdown},
    {"ls", "list files", cmd_ls},
    {"help", "list commands", cmd_help},
};

// entries in commmand table
#define NCMDS (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(int c, char **v) {
  (void)c;
  (void)v;
  for (size_t i = 0; i < NCMDS; i++)
    kprintf("  %s - %s\n", commands[i].name, commands[i].help);
}

static void run_line(char *s) {
  char *argv[16];
  int argc = 0;
  while (*s && argc < 16) {
    while (*s == ' ')
      *s++ = '\0';
    if (!*s)
      break;
    argv[argc++] = s;
    while (*s && *s != ' ')
      s++;
  }
  if (argc == 0)
    return;

  for (size_t i = 0; i < NCMDS; i++) {
    if (strcmp(argv[0], commands[i].name) == 0) {
      commands[i].fn(argc, argv);
      return;
    }
  }
  kprintf("Unknown command: %s\n", argv[0]);
}

// line editor
static void line_input(char ch) {
  if (ch == '\n') {
    kprintf("\n");
    line[line_len] = '\0';
    run_line(line);
    line_len = 0;
    kprintf("tusos-->");
  } else if (ch == '\b') {
    if (line_len) {
      line_len--;
      kprintf("\b \b");
    }
  } else if (line_len < sizeof(line) - 1) {
    line[line_len++] = ch;
    kprintf("%c", ch);
  }
}

// scancode decorder
static void handle_scancode(uint8_t raw) {
  static bool extended;
  if (raw == 0xE0) { // prefix for arrows, right ctrl, etc
    extended = true;
    return;
  }
  bool ext = extended;
  extended = false;
  if (ext)
    return;

  bool released = raw & 0x80;
  uint8_t code = raw & 0x7F;
  uint16_t key = lowercase[code];

  switch (key) {
  case LSHFT:
  case RSHFT:
    shift_down = !released;
    return;
  case CAPS:
    if (!released)
      caps_lock = !caps_lock;
    return;
  default:
    break;
  }

  if (released || key > 0x7F)
    return;

  bool letter = key >= 'a' && key <= 'z';
  bool upper = shift_down ^ (caps_lock && letter);
  line_input((char)(upper ? uppercase[code] : key));
}

__attribute__((interrupt)) void keyboardhandler(struct IDTEFrame *frame) {
  (void)frame;
  uint8_t sc = inPortB(0x60);
  uint32_t next = (kbd_head + 1) % KBD_BUF;
  if (next != kbd_tail) {
    kbd_buf[kbd_head] = sc;
    kbd_head = next;
  }
  end_of_interrupt();
}

void keyboard_poll(void) {
  while (kbd_tail != kbd_head) {
    uint8_t sc = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF;
    handle_scancode(sc);
  }
}

void keyboard_idle(void) {
  keyboard_poll();
  if (kbd_tail == kbd_head)
    sleep(1);
}

void shell_run(void) {
  kprintf("[shell_run entered]\n");
  kprintf("tusos-->");
  for (;;)
    keyboard_idle();
}

void initKeyboard(void) {
  uint32_t gsi = kernel.irq_overrides[1];
  set_ioapic_entry(0x21, 1, 0, 0);
  unmask_ioapic(gsi, 0);
}
