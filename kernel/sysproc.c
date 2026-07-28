#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 va;    // 起始虚拟页地址
  int npages;   // 需要检测的页面数量
  uint64 buf;   // 用户态存放掩码的缓冲区

  // 取出三个系统调用参数
  if(argaddr(0, &va) < 0) return -1;
  if(argint(1, &npages) < 0) return -1;

  // 限制最多64页，只用1个uint64保存位掩码
  if(npages > 64) return -1;
  uint64 mask = 0;
  struct proc *p = myproc();

  for(int i = 0; i < npages; i++){
    uint64 cur_va = va + i * PGSIZE;
    // 查找当前虚拟地址对应的PTE，不分配页表
    pte_t *pte = walk(p->pagetable, cur_va, 0);
    if(pte == 0) continue;
    // 判断是否设置访问位PTE_A
    if(*pte & PTE_A){
      mask |= (1UL << i);
      *pte &= ~PTE_A; // 清除访问位，方便下一次检测
    }
  }
  // 将内核掩码拷贝到用户缓冲区
  if(argaddr(2, &buf) < 0) return -1;
  if(copyout(p->pagetable, buf, (char*)&mask, sizeof(mask)) < 0)
    return -1;
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
