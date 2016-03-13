// os.c - based on xv6 with heavy modifications
#include <u.h>

enum {TICK_NUM = 1000};

enum {
  PAGE    = 4096,       // page size
  NPROC   = 64,         // maximum number of processes
  NOFILE  = 16,         // open files per process
  NFILE   = 100,        // open files per system
  NBUF    = 10,         // size of disk block cache
  NINODE  = 50,         // maximum number of active i-nodes  XXX make this more dynamic ... 
  NDEV    = 10,         // maximum major device number
  USERTOP = 0xc0000000, // end of user address space
  P2V     = +USERTOP,   // turn a physical address into a virtual address
  V2P     = -USERTOP,   // turn a virtual address into a physical address
  FSSIZE  = PAGE*1024,  // XXX
  MAXARG  = 256,        // max exec arguments
  STACKSZ = 0x800000,   // user stack size (8MB)
};

enum { // page table entry flags   XXX refactor vs. i386
  PTE_P = 0x001, // present
  PTE_W = 0x002, // writeable
  PTE_U = 0x004, // user
  PTE_A = 0x020, // accessed
  PTE_D = 0x040, // dirty
};

enum { // processor fault codes
  FMEM,   // bad physical address
  FTIMER, // timer interrupt
  FKEYBD, // keyboard interrupt
  FPRIV,  // privileged instruction
  FINST,  // illegal instruction
  FSYS,   // software trap
  FARITH, // arithmetic trap
  FIPAGE, // page fault on opcode fetch
  FWPAGE, // page fault on write
  FRPAGE, // page fault on read
  USER=16 // user mode exception
};

struct trapframe { // layout of the trap frame built on the stack by trap handler
  int sp, pad1;
  double g, f;
  int c,  pad2;
  int b,  pad3;
  int a,  pad4;
  int fc, pad5;
  int pc, pad6;
};

struct buf {
  int flags;
  uint sector;
  struct buf *prev;      // LRU cache list
  struct buf *next;
//  struct buf *qnext;     // disk queue XXX
  uchar *data;
};
enum { B_BUSY  = 1,      // buffer is locked by some process
       B_VALID = 2,      // buffer has been read from disk
       B_DIRTY = 4};     // buffer needs to be written to disk
enum { S_IFIFO = 0x1000, // fifo
       S_IFCHR = 0x2000, // character
       S_IFBLK = 0x3000, // block
       S_IFDIR = 0x4000, // directory
       S_IFREG = 0x8000, // regular
       S_IFMT  = 0xF000 }; // file type mask
enum { O_RDONLY, O_WRONLY, O_RDWR, O_CREAT = 0x100, O_TRUNC = 0x200 };
enum { SEEK_SET, SEEK_CUR, SEEK_END };

struct stat {
  ushort st_dev;         // device number
  ushort st_mode;        // type of file
  uint   st_ino;         // inode number on device
  uint   st_nlink;       // number of links to file
  uint   st_size;        // size of file in bytes
};

// disk file system format
enum {
  ROOTINO  = 16,         // root i-number
  NDIR     = 480,
  NIDIR    = 512,
  NIIDIR   = 8,
  NIIIDIR  = 4,
  DIRSIZ   = 252,
  PIPESIZE = 4000,       // XXX up to a page (since pipe is a page)
};

struct dinode { // on-disk inode structure
  ushort mode;           // file mode
  uint nlink;            // number of links to inode in file system
  uint size;             // size of file
  uint pad[17];
  uint dir[NDIR];        // data block addresses
  uint idir[NIDIR];
  uint iidir[NIIDIR];    // XXX not implemented
  uint iiidir[NIIIDIR];  // XXX not implemented
};

struct direct { // directory is a file containing a sequence of direct structures.
  uint d_ino;
  char d_name[DIRSIZ];
};

struct pipe {
  char data[PIPESIZE];
  uint nread;            // number of bytes read
  uint nwrite;           // number of bytes written
  int readopen;          // read fd is still open
  int writeopen;         // write fd is still open
};

struct inode { // in-memory copy of an inode
  uint inum;             // inode number
  int ref;               // reference count
  int flags;             // I_BUSY, I_VALID
  ushort mode;           // copy of disk inode
  uint nlink;
  uint size;
  uint dir[NDIR];
  uint idir[NIDIR];
};

enum { FD_NONE, FD_PIPE, FD_INODE, FD_SOCKET, FD_RFS };
struct file {
  int type;
  int ref;
  char readable;
  char writable;
  struct pipe *pipe;     // XXX make vnode
  struct inode *ip;
  uint off;
};

enum { I_BUSY = 1, I_VALID = 2 };
enum { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc { // per-process state
  struct proc *next;
  struct proc *prev;
  uint sz;               // size of process memory (bytes)
  uint *pdir;            // page directory
  char *kstack;          // bottom of kernel stack for this process
  int state;             // process state
  int pid;               // process ID
  struct proc *parent;   // parent process
  struct trapframe *tf;  // trap frame for current syscall
  int context;           // swtch() here to run process
  void *chan;            // if non-zero, sleeping on chan
  int killed;            // if non-zero, have been killed
  struct file *ofile[NOFILE]; // open files
  struct inode *cwd;     // current directory
  char name[16];         // process name (debugging)
};

struct devsw { // device implementations XXX redesign
  int (*read)();
  int (*write)();
};

enum { CONSOLE = 1 }; // XXX ditch..

enum { INPUT_BUF = 128 };
struct input_s {
  char buf[INPUT_BUF];
  uint r;  // read index
  uint w;  // write index
};

enum { PF_INET = 2, AF_INET = 2, SOCK_STREAM = 1, INADDR_ANY = 0 }; // XXX keep or chuck these?

// *** Globals ***

struct proc proc[NPROC];
struct proc *u;          // current process
struct proc *init;
char *mem_free;          // memory free list
char *mem_top;           // current top of unused memory
uint mem_sz;             // size of physical memory
uint kreserved;          // start of kernel reserved memory heap
struct devsw devsw[NDEV];
uint *kpdir;             // kernel page directory
uint ticks;
char *memdisk;
struct input_s input;    // XXX do this some other way?
struct buf bcache[NBUF];
struct buf bfreelist;    // linked list of all buffers, through prev/next.   bfreelist.next is most recently used
struct inode inode[NINODE]; // inode cache XXX make dynamic and eventually power of 2, look into iget()
struct file file[NFILE];
int nextpid;

rfsd = -1; // XXX will be set on mount, XXX total redesign?

// *** Code ***

void *memcpy(void *d, void *s, uint n) { asm(LL,8); asm(LBL, 16); asm(LCL,24); asm(MCPY); asm(LL,8); }
void *memset(void *d, uint c,  uint n) { asm(LL,8); asm(LBLB,16); asm(LCL,24); asm(MSET); asm(LL,8); }
void *memchr(void *s, uint c,  uint n) { asm(LL,8); asm(LBLB,16); asm(LCL,24); asm(MCHR); }

int in(port)    { asm(LL,8); asm(BIN); }
out(port, val)  { asm(LL,8); asm(LBL,16); asm(BOUT); }
ivec(void *isr) { asm(LL,8); asm(IVEC); }
lvadr()         { asm(LVAD); }
uint msiz()     { asm(MSIZ); }
stmr(val)       { asm(LL,8); asm(TIME); }
pdir(val)       { asm(LL,8); asm(PDIR); }
spage(val)      { asm(LL,8); asm(SPAG); }
splhi()         { asm(CLI); }
splx(int e)     { if (e) asm(STI); }

int strlen(void *s) { return memchr(s, 0, -1) - s; }

xstrncpy(char *s, char *t, int n) // no return value unlike strncpy XXX remove me only called once
{
  while (n-- > 0 && (*s++ = *t++));
  while (n-- > 0) *s++ = 0;
}

safestrcpy(char *s, char *t, int n) // like strncpy but guaranteed to null-terminate.
{
  if (n <= 0) return;
  while (--n > 0 && (*s++ = *t++));
  *s = 0;
}

// console device
cout(char c)
{
  out(1, c);
}
printn(int n)
{
  if (n > 9) { printn(n / 10); n %= 10; }
  cout(n + '0');
}
printx(uint n)
{
  if (n > 15) { printx(n >> 4); n &= 15; }
  cout(n + (n > 9 ? 'a' - 10 : '0'));
}
printf(char *f, ...) // XXX simplify or chuck
{
  int n, e = splhi(); char *s; va_list v;
  va_start(v, f);
  while (*f) {
    if (*f != '%') { cout(*f++); continue; }
    switch (*++f) {
    case 'd': f++; if ((n = va_arg(v,int)) < 0) { cout('-'); printn(-n); } else printn(n); continue;
    case 'x': f++; printx(va_arg(v,int)); continue;
    case 's': f++; for (s = va_arg(v, char *); *s; s++) cout(*s); continue;
    }
    cout('%');
  }
  splx(e);
}

panic(char *s)
{
  asm(CLI);
  out(1,'p'); out(1,'a'); out(1,'n'); out(1,'i'); out(1,'c'); out(1,':'); out(1,' '); 
  while (*s) out(1,*s++);
  out(1,'\n');
  asm(HALT);
}

consoleintr()
{
  int c;
  while ((c = in(0)) != -1) {
//    printf("<%d>",c); //   XXX
    if (input.w - input.r < INPUT_BUF) {
      input.buf[input.w++ % INPUT_BUF] = c;
    }
  }
}

int consoleread(struct inode *ip, char *dst, int n)
{
  int target, c, e;

  target = n;
  e = splhi();
  while (n > 0) {
    if (input.r == input.w && n < target) break; // block until at least one byte transfered
    while (input.r == input.w) {
      if (u->killed) {
        splx(e);
        return -1;
      }
    }
    c = input.buf[input.r++ % INPUT_BUF];
    *dst++ = c;  // XXX pagefault possible in cli (perhaps use inode locks to achieve desired effect)
    n--;
  }
  splx(e);

  return target - n;
}

int consolewrite(struct inode *ip, char *buf, int n)
{
  int i, e;

  e = splhi(); // XXX pagefault possible in cli
  for (i = 0; i < n; i++) cout(buf[i]);
  splx(e);
  return n;
}

consoleinit()
{
  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read  = consoleread;
}

// fake IDE disk; stores blocks in memory.  useful for running kernel without scratch disk.  XXX but no good for stressing demand pageing logic!
ideinit()
{
  memdisk = P2V+(mem_sz - FSSIZE);
}

trap(uint *sp, double g, double f, int c, int b, int a, int fc, uint *pc)  
{
  uint va;
  switch (fc) {

  case FTIMER: 
  case FTIMER + USER: 
    ticks++;
    if(ticks % TICK_NUM == 0){
        cout('+');
    }
    return;
    
  case FKEYBD:
  case FKEYBD + USER:
    consoleintr();
    return; //??XXX postkill?
  }
}

alltraps()
{
  asm(PSHA);
  asm(PSHB);
  asm(PSHC);
  asm(PSHF);
  asm(PSHG);
  asm(LUSP); asm(PSHA);
  trap();                // registers passed back out by magic reference :^O
  asm(POPA); asm(SUSP);
  asm(POPG);
  asm(POPF);
  asm(POPC);
  asm(POPB);
  asm(POPA);
  asm(RTI);
}

mainc()
{
  kpdir[0] = 0;          // don't need low map anymore
  consoleinit();         // console device
  ivec(alltraps);        // trap vector
  ideinit();             // disk
  stmr(128*1024);        // set timer
  asm(STI);
  printf("Welcome!\n");

  /* do nothing */
  while(1);
}

main()
{
  int *ksp;              // temp kernel stack pointer
  static char kstack[256]; // temp kernel stack
  static int endbss;     // last variable in bss segment
    
  // initialize memory allocation
  mem_top = kreserved = ((uint)&endbss + PAGE + 3) & -PAGE; 
  mem_sz = msiz();

  // initialize kernel stack pointer
  ksp = ((uint)kstack + sizeof(kstack) - 8) & -8;
  *ksp = (uint)mainc;
  asm(LL, 4);
  asm(SSP);

  asm(LEV);
}
