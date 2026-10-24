#include "keyboard.h"
#include "apic.h"
#include "apic_timer.h"
#include "ext2.h"
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
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* ---------- low-level helpers ---------- */
static inline uint64_t k_rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}

static inline void k_cpuid(uint32_t leaf, uint32_t sub, uint32_t *a,
                           uint32_t *b, uint32_t *c, uint32_t *d) {
  asm volatile("cpuid"
               : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
               : "a"(leaf), "c"(sub));
}

static inline void k_outl(uint16_t port, uint32_t v) {
  asm volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t k_inl(uint16_t port) {
  uint32_t v;
  asm volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
  return v;
}

#define READ_CR(n)                                                             \
  ({                                                                           \
    uint64_t _v;                                                               \
    asm volatile("mov %%cr" #n ", %0" : "=r"(_v));                             \
    _v;                                                                        \
  })

struct __attribute__((packed)) dtr {
  uint16_t limit;
  uint64_t base;
};

/* ---------- printing helpers ---------- */
static void hex64(uint64_t v, char *b) { /* b needs 17 bytes */
  for (int i = 15; i >= 0; i--) {
    b[i] = "0123456789abcdef"[v & 0xF];
    v >>= 4;
  }
  b[16] = '\0';
}

static void print_reg(const char *name, uint64_t v) {
  char b[17];
  hex64(v, b);
  kprintf("  %s 0x%s\n", name, b);
}

struct bitname {
  uint8_t bit;
  const char *name;
};

static void print_flags(const struct bitname *t, size_t n, uint64_t v) {
  kprintf("           ");
  for (size_t i = 0; i < n; i++)
    if (v & (1ULL << t[i].bit))
      kprintf(" %s", t[i].name);
  kprintf("\n");
}

static void put4(char *p, uint32_t v) { /* 4 bytes, little endian */
  for (int i = 0; i < 4; i++) {
    p[i] = (char)(v & 0xFF);
    v >>= 8;
  }
}

static bool parse_num(const char *s, uint64_t *out) {
  uint64_t v = 0;
  uint32_t base = 10;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    base = 16;
    s += 2;
  }
  if (!*s)
    return false;
  for (; *s; s++) {
    uint32_t d;
    if (*s >= '0' && *s <= '9')
      d = (uint32_t)(*s - '0');
    else if (*s >= 'a' && *s <= 'f')
      d = (uint32_t)(*s - 'a' + 10);
    else if (*s >= 'A' && *s <= 'F')
      d = (uint32_t)(*s - 'A' + 10);
    else
      return false;
    if (d >= base)
      return false;
    v = v * base + d;
  }
  *out = v;
  return true;
}

/* ---------- flag name tables ---------- */
static const struct bitname cr0_bits[] = {
    {0, "PE"},  {1, "MP"},  {2, "EM"},  {3, "TS"},  {4, "ET"}, {5, "NE"},
    {16, "WP"}, {18, "AM"}, {29, "NW"}, {30, "CD"}, {31, "PG"}};
static const struct bitname cr4_bits[] = {
    {0, "VME"},   {1, "PVI"},       {2, "TSD"},         {3, "DE"},
    {4, "PSE"},   {5, "PAE"},       {6, "MCE"},         {7, "PGE"},
    {8, "PCE"},   {9, "OSFXSR"},    {10, "OSXMMEXCPT"}, {11, "UMIP"},
    {12, "LA57"}, {16, "FSGSBASE"}, {17, "PCIDE"},      {18, "OSXSAVE"},
    {20, "SMEP"}, {21, "SMAP"}};
static const struct bitname rflags_bits[] = {{0, "CF"}, {2, "PF"},  {4, "AF"},
                                             {6, "ZF"}, {7, "SF"},  {8, "TF"},
                                             {9, "IF"}, {10, "DF"}, {11, "OF"}};
static const struct bitname efer_bits[] = {
    {0, "SCE"}, {8, "LME"}, {10, "LMA"}, {11, "NXE"}};

static const struct bitname feat1_edx[] = {
    {0, "fpu"},  {4, "tsc"},   {5, "msr"},  {6, "pae"},
    {9, "apic"}, {11, "sep"},  {13, "pge"}, {15, "cmov"},
    {23, "mmx"}, {24, "fxsr"}, {25, "sse"}, {26, "sse2"}};
static const struct bitname feat1_ecx[] = {
    {0, "sse3"},    {9, "ssse3"},   {13, "cx16"},      {19, "sse4.1"},
    {20, "sse4.2"}, {21, "x2apic"}, {23, "popcnt"},    {26, "xsave"},
    {28, "avx"},    {30, "rdrand"}, {31, "hypervisor"}};
static const struct bitname feat_ext_edx[] = {
    {11, "syscall"}, {20, "nx"}, {26, "1gb-pages"}, {27, "rdtscp"}, {29, "lm"}};

/* ---------- regs ---------- */
static void cmd_regs(int c, char **v) {
  (void)c;
  (void)v;
  uint64_t cr0 = READ_CR(0), cr2 = READ_CR(2), cr3 = READ_CR(3),
           cr4 = READ_CR(4), efer = k_rdmsr(0xC0000080), rfl, rsp;
  uint16_t cs, ss, ds, es, fs, gs, tr;
  struct dtr gdtr, idtr;

  asm volatile("pushfq; pop %0" : "=r"(rfl));
  asm volatile("mov %%rsp, %0" : "=r"(rsp));
  asm volatile("mov %%cs, %0" : "=r"(cs));
  asm volatile("mov %%ss, %0" : "=r"(ss));
  asm volatile("mov %%ds, %0" : "=r"(ds));
  asm volatile("mov %%es, %0" : "=r"(es));
  asm volatile("mov %%fs, %0" : "=r"(fs));
  asm volatile("mov %%gs, %0" : "=r"(gs));
  asm volatile("str %0" : "=r"(tr));
  asm volatile("sgdt %0" : "=m"(gdtr));
  asm volatile("sidt %0" : "=m"(idtr));

  kprintf("Control registers\n");
  print_reg("CR0   ", cr0);
  print_flags(cr0_bits, ARRAY_SIZE(cr0_bits), cr0);
  print_reg("CR2   ", cr2);
  print_reg("CR3   ", cr3);
  print_reg("  PML4", cr3 & ~0xFFFULL);
  print_reg("CR4   ", cr4);
  print_flags(cr4_bits, ARRAY_SIZE(cr4_bits), cr4);
  print_reg("EFER  ", efer);
  print_flags(efer_bits, ARRAY_SIZE(efer_bits), efer);

  kprintf("Execution state\n");
  print_reg("RFLAGS", rfl);
  print_flags(rflags_bits, ARRAY_SIZE(rflags_bits), rfl);
  print_reg("RSP   ", rsp);
  kprintf("  CPL    %d\n", (int)(cs & 3));

  kprintf("Segments\n");
  print_reg("CS    ", cs);
  print_reg("SS    ", ss);
  print_reg("DS    ", ds);
  print_reg("ES    ", es);
  print_reg("FS    ", fs);
  print_reg("GS    ", gs);
  print_reg("TR    ", tr);

  kprintf("Descriptor tables\n");
  print_reg("GDT.base ", gdtr.base);
  print_reg("GDT.limit", gdtr.limit);
  print_reg("IDT.base ", idtr.base);
  print_reg("IDT.limit", idtr.limit);
}

/* ---------- cpu ---------- */
static void cmd_cpu(int c, char **v) {
  (void)c;
  (void)v;
  uint32_t a, b, cx, d, maxleaf, maxext;
  char vendor[13], brand[49];

  k_cpuid(0, 0, &maxleaf, &b, &cx, &d);
  put4(vendor, b);
  put4(vendor + 4, d);
  put4(vendor + 8, cx);
  vendor[12] = '\0';

  k_cpuid(0x80000000, 0, &maxext, &b, &cx, &d);
  brand[0] = '\0';
  if (maxext >= 0x80000004) {
    for (uint32_t i = 0; i < 3; i++) {
      k_cpuid(0x80000002 + i, 0, &a, &b, &cx, &d);
      put4(brand + i * 16, a);
      put4(brand + i * 16 + 4, b);
      put4(brand + i * 16 + 8, cx);
      put4(brand + i * 16 + 12, d);
    }
    brand[48] = '\0';
  }
  const char *bs = brand;
  while (*bs == ' ')
    bs++;

  k_cpuid(1, 0, &a, &b, &cx, &d);
  uint32_t ecx1 = cx, edx1 = d;
  uint32_t basefam = (a >> 8) & 0xF, family = basefam, model = (a >> 4) & 0xF;
  if (basefam == 0xF)
    family += (a >> 20) & 0xFF;
  if (basefam == 0x6 || basefam == 0xF)
    model |= ((a >> 16) & 0xF) << 4;

  kprintf("Vendor    %s\n", vendor);
  kprintf("Brand     %s\n", bs);
  kprintf("Family    %d  model %d  stepping %d\n", (int)family, (int)model,
          (int)(a & 0xF));
  kprintf("APIC ID   %d   max cpuid leaf %d\n", (int)(b >> 24), (int)maxleaf);

  if (maxext >= 0x80000008) {
    k_cpuid(0x80000008, 0, &a, &b, &cx, &d);
    kprintf("Address   %d-bit physical, %d-bit virtual\n", (int)(a & 0xFF),
            (int)((a >> 8) & 0xFF));
  }

  kprintf("Features ");
  print_flags(feat1_edx, ARRAY_SIZE(feat1_edx), edx1);
  kprintf("         ");
  print_flags(feat1_ecx, ARRAY_SIZE(feat1_ecx), ecx1);
  if (maxext >= 0x80000001) {
    k_cpuid(0x80000001, 0, &a, &b, &cx, &d);
    kprintf("         ");
    print_flags(feat_ext_edx, ARRAY_SIZE(feat_ext_edx), d);
  }
}

/* ---------- host ---------- */
static const struct {
  const char *sig;
  const char *name;
} hypervisors[] = {
    {"TCGTCGTCGTCG", "QEMU (TCG software emulation)"},
    {"KVMKVMKVM", "KVM (QEMU with hardware acceleration)"},
    {"Microsoft Hv", "Microsoft Hyper-V"},
    {"VMwareVMware", "VMware"},
    {"VBoxVBoxVBox", "VirtualBox"},
    {"XenVMMXenVMM", "Xen"},
    {"bhyve bhyve ", "bhyve"},
};

static void cmd_host(int c, char **v) {
  (void)c;
  (void)v;
  uint32_t a, b, cx, d;
  char sig[13];

  k_cpuid(1, 0, &a, &b, &cx, &d);
  if (!(cx & (1u << 31))) {
    kprintf("Hypervisor  none reported (bare metal, or hidden)\n");
  } else {
    k_cpuid(0x40000000, 0, &a, &b, &cx, &d);
    put4(sig, b);
    put4(sig + 4, cx);
    put4(sig + 8, d);
    sig[12] = '\0';

    const char *name = "unknown";
    for (size_t i = 0; i < ARRAY_SIZE(hypervisors); i++)
      if (strcmp(sig, hypervisors[i].sig) == 0)
        name = hypervisors[i].name;
    kprintf("Hypervisor  %s  (signature \"%s\")\n", name, sig);
  }

  k_outl(0xCF8, 0x80000000u); /* PCI 0:0.0 vendor/device */
  uint32_t id = k_inl(0xCFC);
  const char *chipset = "unknown";
  if (id == 0x12378086u)
    chipset = "Intel 440FX (QEMU -M pc)";
  else if (id == 0x29C08086u)
    chipset = "Intel Q35 (QEMU -M q35)";
  kprintf("Chipset     %s\n", chipset);

  char b1[17], b2[17];
  hex64(kernel.hhdm, b1);
  hex64(kernel.apic_ticks, b2);
  kprintf("HHDM        0x%s\n", b1);
  kprintf("Timer ticks 0x%s\n", b2);
  kprintf("PCI devices %d   NVMe ctrls %d   ext2 mounts %d\n",
          kernel.pci_device_count, kernel.nvme_controller_ptr,
          kernel.ext2_partition_count);
}

/* ---------- msr ---------- */
static const struct {
  uint32_t msr;
  const char *name;
} known_msrs[] = {
    {0x0000001B, "IA32_APIC_BASE"}, {0x00000277, "IA32_PAT"},
    {0xC0000080, "IA32_EFER"},      {0xC0000081, "IA32_STAR"},
    {0xC0000082, "IA32_LSTAR"},     {0xC0000100, "IA32_FS_BASE"},
    {0xC0000101, "IA32_GS_BASE"},   {0xC0000102, "IA32_KERNEL_GS_BASE"},
};

static void cmd_msr(int argc, char **argv) {
  if (argc < 2) { /* no argument: dump the safe, known set */
    for (size_t i = 0; i < ARRAY_SIZE(known_msrs); i++) {
      char b[17];
      hex64(k_rdmsr(known_msrs[i].msr), b);
      kprintf("  0x%x  %s = 0x%s\n", known_msrs[i].msr, known_msrs[i].name, b);
    }
    kprintf("usage: msr [number]   (an unknown MSR faults)\n");
    return;
  }
  uint64_t n;
  if (!parse_num(argv[1], &n)) {
    kprintf("msr: bad number '%s'\n", argv[1]);
    return;
  }
  print_reg("MSR value", k_rdmsr((uint32_t)n));
}

/* ---------- cpuid ---------- */
static void cmd_cpuid(int argc, char **argv) {
  uint64_t leaf = 0, sub = 0;
  if (argc < 2 || !parse_num(argv[1], &leaf) ||
      (argc > 2 && !parse_num(argv[2], &sub))) {
    kprintf("usage: cpuid <leaf> [subleaf]\n");
    return;
  }
  uint32_t a, b, cx, d;
  k_cpuid((uint32_t)leaf, (uint32_t)sub, &a, &b, &cx, &d);
  print_reg("EAX", a);
  print_reg("EBX", b);
  print_reg("ECX", cx);
  print_reg("EDX", d);
}
/* ================= inode commands ================= */
static char
    scratch[4096]; /* one fs block; validate_sb caps block size at 4096 */

static struct ext2_partition *need_fs(void) {
  if (!kernel.root_fs)
    kprintf("no ext2 filesystem mounted\n");
  return kernel.root_fs;
}

static uint16_t rd16(const char *p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}
static uint32_t rd32(const char *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
static char dg(uint32_t v) { return (char)('0' + v % 10); }

static void dec64(uint64_t v, char *b) { /* b needs 21 bytes */
  char t[21];
  int n = 0;
  do {
    t[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  for (int i = 0; i < n; i++)
    b[i] = t[n - 1 - i];
  b[n] = '\0';
}

static void fmt_time(uint32_t t, char *o) { /* o needs 20 bytes */
  if (!t) {
    o[0] = '-';
    o[1] = '\0';
    return;
  }
  uint32_t rem = t % 86400, z = t / 86400 + 719468;
  uint32_t era = z / 146097, doe = z - era * 146097;
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t y = yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2)
    y++;
  o[0] = dg(y / 1000);
  o[1] = dg(y / 100);
  o[2] = dg(y / 10);
  o[3] = dg(y);
  o[4] = '-';
  o[5] = dg(m / 10);
  o[6] = dg(m);
  o[7] = '-';
  o[8] = dg(d / 10);
  o[9] = dg(d);
  o[10] = ' ';
  o[11] = dg(rem / 36000);
  o[12] = dg(rem / 3600);
  o[13] = ':';
  o[14] = dg(rem % 3600 / 600);
  o[15] = dg(rem % 3600 / 60);
  o[16] = ':';
  o[17] = dg(rem % 60 / 10);
  o[18] = dg(rem % 60);
  o[19] = '\0';
}

static void mode_str(uint16_t m, char *s) { /* s needs 11 bytes */
  char t = '?';
  switch (m & 0xF000) {
  case 0x4000:
    t = 'd';
    break;
  case 0x8000:
    t = '-';
    break;
  case 0xA000:
    t = 'l';
    break;
  case 0x2000:
    t = 'c';
    break;
  case 0x6000:
    t = 'b';
    break;
  case 0x1000:
    t = 'p';
    break;
  case 0xC000:
    t = 's';
    break;
  }
  s[0] = t;
  for (int i = 0; i < 9; i++)
    s[1 + i] = (m & (0x100 >> i)) ? "rwxrwxrwx"[i] : '-';
  s[10] = '\0';
}

static const char *type_name(uint16_t m) {
  switch (m & 0xF000) {
  case 0x4000:
    return "directory";
  case 0x8000:
    return "regular file";
  case 0xA000:
    return "symlink";
  case 0x2000:
    return "char device";
  case 0x6000:
    return "block device";
  case 0x1000:
    return "fifo";
  case 0xC000:
    return "socket";
  }
  return "unknown";
}

static const char *ft_name(uint8_t t) {
  static const char *n[] = {"?",   "file", "dir",  "chr",
                            "blk", "fifo", "sock", "symlink"};
  return t < 8 ? n[t] : "?";
}

static uint64_t isize64(const struct ext2_inode *in) {
  uint64_t s = in->i_size;
  if ((in->i_mode & 0xF000) == 0x8000)
    s |= (uint64_t)in->i_dir_acl << 32; /* high bits on rev 1 regular files */
  return s;
}

static bool get_ino(int argc, char **argv, struct ext2_partition *fs,
                    uint32_t *ino) {
  uint64_t n;
  if (argc < 2 || !parse_num(argv[1], &n)) {
    kprintf("usage: %s <inode>\n", argv[0]);
    return false;
  }
  if (n == 0 || n > fs->sb.s_inodes_count) {
    kprintf("inode %d out of range (1..%d)\n", (int)n,
            (int)fs->sb.s_inodes_count);
    return false;
  }
  *ino = (uint32_t)n;
  return true;
}

struct iloc {
  uint32_t group, index, blk, off;
};

static bool inode_locate(struct ext2_partition *fs, uint32_t ino,
                         struct iloc *l) {
  const struct ext2_superblock *sb = &fs->sb;
  if (ino == 0 || ino > sb->s_inodes_count)
    return false;
  l->group = (ino - 1) / sb->s_inodes_per_group;
  l->index = (ino - 1) % sb->s_inodes_per_group;
  struct ext2_group_desc *gd = ext2_get_group_desc(fs, l->group);
  if (!gd)
    return false;
  uint64_t byte = (uint64_t)l->index * fs->inode_size;
  l->blk = gd->bg_inode_table + (uint32_t)(byte / fs->block_size);
  l->off = (uint32_t)(byte % fs->block_size);
  return true;
}

/* ---------- inode <n> ---------- */
static void cmd_inode(int argc, char **argv) {
  struct ext2_partition *fs = need_fs();
  uint32_t ino;
  if (!fs || !get_ino(argc, argv, fs, &ino))
    return;
  struct ext2_inode in;
  if (!ext2_read_inode(fs, ino, &in))
    return;

  char ms[11], sz[21], t[20];
  mode_str(in.i_mode, ms);
  uint64_t size = isize64(&in);
  dec64(size, sz);

  kprintf("inode %d   %s   %s\n", (int)ino, ms, type_name(in.i_mode));
  kprintf("  owner    uid %d  gid %d\n", (int)in.i_uid, (int)in.i_gid);
  kprintf("  size     %s bytes\n", sz);
  kprintf("  links    %d\n", (int)in.i_links_count);
  kprintf("  blocks   %d sectors = %d fs blocks\n", (int)in.i_blocks,
          (int)(in.i_blocks / (fs->block_size / 512)));
  kprintf("  flags    %x\n", (int)in.i_flags);
  fmt_time(in.i_atime, t);
  kprintf("  atime    %s\n", t);
  fmt_time(in.i_ctime, t);
  kprintf("  ctime    %s\n", t);
  fmt_time(in.i_mtime, t);
  kprintf("  mtime    %s\n", t);
  fmt_time(in.i_dtime, t);
  kprintf("  dtime    %s\n", t);

  if ((in.i_mode & 0xF000) == 0xA000 && size < 60) {
    char tgt[61];
    memcpy(tgt, in.i_block, (size_t)size);
    tgt[size] = '\0';
    kprintf("  target   %s   (stored inside the inode)\n", tgt);
    return;
  }

  kprintf("  direct  ");
  int any = 0;
  for (int i = 0; i < 12; i++)
    if (in.i_block[i]) {
      kprintf(" [%d]=%d", i, (int)in.i_block[i]);
      any = 1;
    }
  if (!any)
    kprintf(" none");
  kprintf("\n");
  if (in.i_block[12])
    kprintf("  single indirect  block %d\n", (int)in.i_block[12]);
  if (in.i_block[13])
    kprintf("  double indirect  block %d\n", (int)in.i_block[13]);
  if (in.i_block[14])
    kprintf("  triple indirect  block %d\n", (int)in.i_block[14]);
}

/* ---------- imap <n> ---------- */
static void cmd_imap(int argc, char **argv) {
  struct ext2_partition *fs = need_fs();
  uint32_t ino;
  struct iloc l;
  if (!fs || !get_ino(argc, argv, fs, &ino))
    return;
  if (!inode_locate(fs, ino, &l)) {
    kprintf("imap: cannot locate inode %d\n", (int)ino);
    return;
  }
  struct ext2_group_desc *gd = ext2_get_group_desc(fs, l.group);
  uint64_t byte = (uint64_t)l.index * fs->inode_size;
  uint64_t lba = fs->lba_start + (uint64_t)l.blk * fs->sectors_per_block +
                 l.off / fs->disk->lba_size;
  char lb[21];
  dec64(lba, lb);

  kprintf("inode %d\n", (int)ino);
  kprintf("  group %d of %d, index %d inside the group\n", (int)l.group,
          (int)fs->group_count, (int)l.index);
  kprintf("  descriptor: inode table at block %d, inode bitmap at block %d\n",
          (int)gd->bg_inode_table, (int)gd->bg_inode_bitmap);
  kprintf("  %d * %d bytes = byte %d in the table (table block +%d)\n",
          (int)l.index, (int)fs->inode_size, (int)byte,
          (int)(byte / fs->block_size));
  kprintf("  disk block %d, offset %d in that block\n", (int)l.blk, (int)l.off);
  kprintf("  LBA %s on the disk\n", lb);

  if (ext2_read_blocks(fs, gd->bg_inode_bitmap, scratch)) {
    int bit = (scratch[l.index / 8] >> (l.index % 8)) & 1;
    kprintf("  bitmap: byte %d, bit %d  ->  %s\n", (int)(l.index / 8),
            (int)(l.index % 8), bit ? "allocated" : "FREE");
  }
}

/* ---------- blocks <n> [max] ---------- */
static void print_run(uint64_t fs0, uint64_t len, uint32_t disk) {
  if (disk)
    kprintf("  file %d-%d  ->  disk %d-%d  (%d blocks)\n", (int)fs0,
            (int)(fs0 + len - 1), (int)disk, (int)(disk + len - 1), (int)len);
  else
    kprintf("  file %d-%d  ->  hole\n", (int)fs0, (int)(fs0 + len - 1));
}

static void cmd_blocks(int argc, char **argv) {
  struct ext2_partition *fs = need_fs();
  uint32_t ino;
  if (!fs || !get_ino(argc, argv, fs, &ino))
    return;
  uint64_t limit = 4096;
  if (argc > 2 && !parse_num(argv[2], &limit)) {
    kprintf("usage: blocks <inode> [max blocks]\n");
    return;
  }
  struct ext2_inode in;
  if (!ext2_read_inode(fs, ino, &in))
    return;
  uint64_t size = isize64(&in);
  if ((in.i_mode & 0xF000) == 0xA000 && size < 60) {
    kprintf("fast symlink: target lives in the inode, no data blocks\n");
    return;
  }
  uint64_t nblocks = (size + fs->block_size - 1) / fs->block_size;
  kprintf("inode %d: %d file blocks, indirect roots %d / %d / %d\n", (int)ino,
          (int)nblocks, (int)in.i_block[12], (int)in.i_block[13],
          (int)in.i_block[14]);

  uint64_t rs = 0, rl = 0;
  uint32_t rd = 0;
  int extents = 0;
  uint64_t fb;
  for (fb = 0; fb < nblocks && fb < limit; fb++) {
    uint32_t d;
    if (!ext2_bmap(fs, &in, fb, &d)) {
      kprintf("  bmap failed at file block %d\n", (int)fb);
      break;
    }
    bool extends =
        rl && ((d == 0 && rd == 0) || (d != 0 && rd != 0 && d == rd + rl));
    if (extends) {
      rl++;
      continue;
    }
    if (rl) {
      print_run(rs, rl, rd);
      extents++;
    }
    rs = fb;
    rd = d;
    rl = 1;
  }
  if (rl) {
    print_run(rs, rl, rd);
    extents++;
  }
  kprintf("  %d run(s)", extents);
  if (fb < nblocks)
    kprintf("   (stopped after %d of %d blocks)", (int)fb, (int)nblocks);
  kprintf("\n");
}

/* ---------- ihex <n> ---------- */
static void cmd_ihex(int argc, char **argv) {
  struct ext2_partition *fs = need_fs();
  uint32_t ino;
  struct iloc l;
  if (!fs || !get_ino(argc, argv, fs, &ino) || !inode_locate(fs, ino, &l))
    return;
  if (!ext2_read_blocks(fs, l.blk, scratch)) {
    kprintf("ihex: block read failed\n");
    return;
  }
  kprintf("inode %d raw (%d bytes, block %d offset %d)\n", (int)ino,
          (int)fs->inode_size, (int)l.blk, (int)l.off);
  const uint8_t *p = (const uint8_t *)scratch + l.off;
  for (uint32_t row = 0; row < fs->inode_size; row += 16) {
    char line[16 * 3 + 1];
    uint32_t n = 0;
    for (uint32_t i = row; i < row + 16 && i < fs->inode_size; i++) {
      line[n++] = "0123456789abcdef"[p[i] >> 4];
      line[n++] = "0123456789abcdef"[p[i] & 0xF];
      line[n++] = ' ';
    }
    line[n] = '\0';
    kprintf("  %d: %s\n", (int)row, line);
  }
}

/* ---------- idir <n> ---------- */
static void cmd_idir(int argc, char **argv) {
  struct ext2_partition *fs = need_fs();
  uint32_t ino;
  if (!fs || !get_ino(argc, argv, fs, &ino))
    return;
  struct ext2_inode in;
  if (!ext2_read_inode(fs, ino, &in))
    return;
  if ((in.i_mode & 0xF000) != 0x4000) {
    kprintf("inode %d is not a directory\n", (int)ino);
    return;
  }
  uint64_t nblocks =
      ((uint64_t)in.i_size + fs->block_size - 1) / fs->block_size;
  for (uint64_t fb = 0; fb < nblocks; fb++) {
    uint32_t d;
    if (!ext2_bmap(fs, &in, fb, &d) || d == 0 ||
        !ext2_read_blocks(fs, d, scratch)) {
      kprintf("  dir block %d unreadable\n", (int)fb);
      continue;
    }
    kprintf("dir block %d (disk block %d)\n", (int)fb, (int)d);
    uint32_t pos = 0;
    while (pos + 8 <= fs->block_size) {
      uint32_t eino = rd32(scratch + pos);
      uint16_t rl = rd16(scratch + pos + 4);
      uint8_t nl = (uint8_t)scratch[pos + 6], ft = (uint8_t)scratch[pos + 7];
      if (rl < 8 || (rl & 3) || pos + rl > fs->block_size || nl + 8 > rl) {
        kprintf("  corrupt entry at offset %d\n", (int)pos);
        break;
      }
      char name[256];
      memcpy(name, scratch + pos + 8, nl);
      name[nl] = '\0';
      kprintf("  @%d  ino %d  %s  rec_len %d  name_len %d  \"%s\"%s\n",
              (int)pos, (int)eino, ft_name(ft), (int)rl, (int)nl, name,
              eino ? "" : "  (unused)");
      pos += rl;
    }
  }
}

/* ---------- iscan [start] [count] ---------- */
static void cmd_iscan(int argc, char **argv) {
  struct ext2_partition *fs = need_fs();
  if (!fs)
    return;
  uint64_t start = 1, count = 32;
  if ((argc > 1 && !parse_num(argv[1], &start)) ||
      (argc > 2 && !parse_num(argv[2], &count))) {
    kprintf("usage: iscan [start] [count]\n");
    return;
  }
  if (count > 256)
    count = 256;
  uint32_t cur = 0xFFFFFFFFu;
  int used = 0, freed = 0;
  for (uint64_t i = start; i < start + count && i <= fs->sb.s_inodes_count;
       i++) {
    struct iloc l;
    if (i == 0 || !inode_locate(fs, (uint32_t)i, &l))
      continue;
    if (l.group != cur) {
      struct ext2_group_desc *gd = ext2_get_group_desc(fs, l.group);
      if (!ext2_read_blocks(fs, gd->bg_inode_bitmap, scratch)) {
        kprintf("iscan: bitmap read failed\n");
        return;
      }
      cur = l.group;
    }
    if (!((scratch[l.index / 8] >> (l.index % 8)) & 1)) {
      freed++;
      continue;
    }
    struct ext2_inode in;
    if (!ext2_read_inode(fs, (uint32_t)i, &in))
      continue;
    char ms[11], sz[21];
    mode_str(in.i_mode, ms);
    dec64(isize64(&in), sz);
    kprintf("  %d  %s  links %d  size %s  blocks %d\n", (int)i, ms,
            (int)in.i_links_count, sz,
            (int)(in.i_blocks / (fs->block_size / 512)));
    used++;
  }
  kprintf("  %d allocated, %d free in range\n", used, freed);
}

/* ---------- igroup [g] ---------- */
static void igroup_line(struct ext2_partition *fs, uint32_t g) {
  struct ext2_group_desc *gd = ext2_get_group_desc(fs, g);
  if (!gd || !ext2_read_blocks(fs, gd->bg_inode_bitmap, scratch)) {
    kprintf("  group %d: unreadable\n", (int)g);
    return;
  }
  uint32_t ipg = fs->sb.s_inodes_per_group, used = 0;
  for (uint32_t i = 0; i < ipg; i++)
    used += (scratch[i / 8] >> (i % 8)) & 1;
  uint32_t freec = ipg - used;
  kprintf("  group %d: inodes %d-%d  table blk %d  used %d/%d  free(desc) %d  "
          "free(bitmap) %d  dirs %d  %s\n",
          (int)g, (int)(g * ipg + 1), (int)((g + 1) * ipg),
          (int)gd->bg_inode_table, (int)used, (int)ipg,
          (int)gd->bg_free_inodes_count, (int)freec,
          (int)gd->bg_used_dirs_count,
          freec == gd->bg_free_inodes_count ? "OK" : "MISMATCH");
}

static void cmd_igroup(int argc, char **argv) {
  struct ext2_partition *fs = need_fs();
  if (!fs)
    return;
  if (argc > 1) {
    uint64_t g;
    if (!parse_num(argv[1], &g) || g >= fs->group_count) {
      kprintf("usage: igroup [0..%d]\n", (int)fs->group_count - 1);
      return;
    }
    igroup_line(fs, (uint32_t)g);
    return;
  }
  for (uint32_t g = 0; g < fs->group_count; g++)
    igroup_line(fs, g);
}

static const struct command commands[] = {
    {"clear", "clear the screen", cmd_clear},
    {"reboot", "restart the machine", cmd_reboot},
    {"shutdown", "power off", cmd_shutdown},
    {"ls", "list files", cmd_ls},
    {"help", "list commands", cmd_help},
    {"regs", "control, segment and descriptor registers", cmd_regs},
    {"cpu", "vendor, brand, family, features", cmd_cpu},
    {"host", "hypervisor, chipset, kernel counters", cmd_host},
    {"msr", "msr [n]: known MSRs, or read MSR n", cmd_msr},
    {"cpuid", "cpuid <leaf> [sub]: raw cpuid output", cmd_cpuid},
    {"inode", "inode <n>: decode an inode", cmd_inode},
    {"imap", "imap <n>: where an inode lives on disk", cmd_imap},
    {"blocks", "blocks <n> [max]: file to disk block runs", cmd_blocks},
    {"ihex", "ihex <n>: raw inode bytes", cmd_ihex},
    {"idir", "idir <n>: raw directory entries", cmd_idir},
    {"iscan", "iscan [start] [count]: allocated inodes", cmd_iscan},
    {"igroup", "igroup [g]: per-group inode usage", cmd_igroup},
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
