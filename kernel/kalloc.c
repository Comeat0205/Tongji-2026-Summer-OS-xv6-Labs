// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// lab8
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];


// lab8
void
kinit()
{
  // 逐个初始化每个CPU的锁
  for(int i = 0; i < NCPU; i++){
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// lab8
void
kfree(void *pa)
{
  struct run *r;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  r = (struct run*)pa;

  push_off();       // 关中断，安全获取CPU id
  int cpu = cpuid();
  acquire(&kmem[cpu].lock);
  // 头插法放入当前CPU链表
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
  pop_off();        // 恢复中断
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// lab8
void *
kalloc(void)
{
  struct run *r;
  int cpu;

  // 必须先关中断，才能安全调用cpuid()
  push_off();
  cpu = cpuid();

  // 先从当前CPU自己的空闲链表拿页面
  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r){
    kmem[cpu].freelist = r->next;
    release(&kmem[cpu].lock);
    pop_off();
    // 原有的填充垃圾内存逻辑保留
    memset((char*)r, 5, PGSIZE);
    return r;
  }
  release(&kmem[cpu].lock);

  // 本地链表空，去偷窃其他CPU的页面
  for(int i = 0; i < NCPU; i++){
    if(i == cpu) continue; // 跳过自己，不用再查一遍

    acquire(&kmem[i].lock);
    r = kmem[i].freelist;
    if(r){
      kmem[i].freelist = r->next;
      release(&kmem[i].lock);
      pop_off();
      memset((char*)r, 5, PGSIZE);
      return r;
    }
    release(&kmem[i].lock);
  }

  //所有CPU都没有空闲页，分配失败
  pop_off();
  return 0;
}