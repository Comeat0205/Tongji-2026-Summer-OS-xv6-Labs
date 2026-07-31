//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// Starting virtual address for mmap regions (below TRAPFRAME).
#define MMAPBASE 0x40000000L

// Find a free VMA slot.
static struct vma*
vmaalloc(struct proc *p)
{
  for(int i = 0; i < NVMA; i++){
    if(!p->vma[i].valid)
      return &p->vma[i];
  }
  return 0;
}

// Find the VMA that contains va.
static struct vma*
vmafind(struct proc *p, uint64 va)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vma[i];
    if(v->valid && va >= v->addr && va < v->addr + v->length)
      return v;
  }
  return 0;
}

// Choose a virtual address for a new mapping of 'length' bytes.
static uint64
vmachooseaddr(struct proc *p, uint64 length)
{
  uint64 addr = MMAPBASE;
  int changed;
  do {
    changed = 0;
    for(int i = 0; i < NVMA; i++){
      struct vma *v = &p->vma[i];
      if(v->valid && addr < v->addr + v->length && addr + length > v->addr){
        addr = PGROUNDUP(v->addr + v->length);
        changed = 1;
      }
    }
  } while(changed);

  if(addr + length > TRAPFRAME)
    return 0;
  return addr;
}

// Write one mapped page back to the file (MAP_SHARED).
static void
vmawriteback(struct proc *p, struct vma *v, uint64 va)
{
  uint64 pa;
  struct file *f = v->f;
  int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
  uint64 off = v->offset + (va - v->addr);
  int n = PGSIZE;
  int i = 0;

  if((v->flags & MAP_SHARED) == 0)
    return;
  if((v->prot & PROT_WRITE) == 0)
    return;
  if(f == 0 || f->writable == 0)
    return;

  pa = walkaddr(p->pagetable, va);
  if(pa == 0)
    return;

  while(i < n){
    int n1 = n - i;
    if(n1 > max)
      n1 = max;
    begin_op();
    ilock(f->ip);
    writei(f->ip, 0, pa + i, off + i, n1);
    iunlock(f->ip);
    end_op();
    i += n1;
  }
}

// Unmap pages starting at va within VMA v; write back if needed.
static void
vmaunmap(struct proc *p, struct vma *v, uint64 va, uint64 size)
{
  for(uint64 a = va; a < va + size; a += PGSIZE){
    uint64 pa = walkaddr(p->pagetable, a);
    if(pa == 0)
      continue;
    vmawriteback(p, v, a);
    uvmunmap(p->pagetable, a, 1, 1);
  }
}

// Unmap all VMAs for process p (used by exit).
void
vmafree(struct proc *p)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vma[i];
    if(!v->valid)
      continue;
    vmaunmap(p, v, v->addr, v->length);
    if(v->f){
      fileclose(v->f);
      v->f = 0;
    }
    v->valid = 0;
    v->length = 0;
  }
}

// Handle a page fault in an mmap region.
// write==1 if it was a store fault.
// Returns 0 on success, -1 on failure.
int
mmapfault(uint64 va, int write)
{
  struct proc *p = myproc();
  struct vma *v;
  char *mem;
  uint64 off;
  int perm;

  if(va >= MAXVA)
    return -1;
  va = PGROUNDDOWN(va);

  v = vmafind(p, va);
  if(v == 0)
    return -1;
  if(write && (v->prot & PROT_WRITE) == 0)
    return -1;
  if(!write && (v->prot & PROT_READ) == 0 && (v->prot & PROT_WRITE) == 0)
    return -1;

  mem = kalloc();
  if(mem == 0)
    return -1;
  memset(mem, 0, PGSIZE);

  off = v->offset + (va - v->addr);
  ilock(v->f->ip);
  readi(v->f->ip, 0, (uint64)mem, off, PGSIZE);
  iunlock(v->f->ip);

  perm = PTE_U;
  if(v->prot & PROT_READ)
    perm |= PTE_R;
  if(v->prot & PROT_WRITE)
    perm |= PTE_W | PTE_R;
  if(v->prot & PROT_EXEC)
    perm |= PTE_X;

  if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0){
    kfree(mem);
    return -1;
  }
  return 0;
}

uint64
sys_mmap(void)
{
  uint64 addr, length;
  int prot, flags, fd, offset;
  struct file *f;
  struct proc *p = myproc();
  struct vma *v;

  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 ||
     argint(2, &prot) < 0 || argint(3, &flags) < 0 ||
     argint(4, &fd) < 0 || argint(5, &offset) < 0)
    return -1;

  if(length == 0)
    return -1;
  if(offset != 0)
    return -1;
  if(addr != 0)
    return -1;
  if((flags & MAP_SHARED) == 0 && (flags & MAP_PRIVATE) == 0)
    return -1;

  if(fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;
  if(f->type != FD_INODE)
    return -1;
  if((prot & PROT_WRITE) && (flags & MAP_SHARED) && f->writable == 0)
    return -1;
  if(f->readable == 0)
    return -1;

  length = PGROUNDUP(length);

  v = vmaalloc(p);
  if(v == 0)
    return -1;

  addr = vmachooseaddr(p, length);
  if(addr == 0)
    return -1;

  v->valid = 1;
  v->addr = addr;
  v->length = length;
  v->prot = prot;
  v->flags = flags;
  v->offset = offset;
  v->f = filedup(f);

  return addr;
}

uint64
sys_munmap(void)
{
  uint64 addr, length;
  struct proc *p = myproc();
  struct vma *v;

  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0)
    return -1;
  if(length == 0)
    return -1;
  if(addr % PGSIZE)
    return -1;

  length = PGROUNDUP(length);

  v = vmafind(p, addr);
  if(v == 0)
    return -1;
  if(!(addr == v->addr || addr + length == v->addr + v->length))
    return -1;
  if(addr + length > v->addr + v->length)
    return -1;

  vmaunmap(p, v, addr, length);

  if(addr == v->addr && length == v->length){
    fileclose(v->f);
    v->f = 0;
    v->valid = 0;
    v->length = 0;
  } else if(addr == v->addr){
    v->addr += length;
    v->offset += length;
    v->length -= length;
  } else {
    v->length -= length;
  }
  return 0;
}
