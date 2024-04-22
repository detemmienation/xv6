// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
#include <stdint.h>
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "cache.h"
#include "proc.h"


////////////////////////////////////

#define MAX_ORDER 23 // 2^14 = 16384 (16K), but considering sizes up to 8M, we adjust the order accordingly
#define BIAS 0x800000L  

typedef struct buddy_block //数据结构本身要16B，不能分配比他还小的空间
{
  struct buddy_block *next; // 指向同一大小的下一个空闲块
  char*  space;
  int order;                // 块的大小级别，0-14分别对应256B-8M
} buddy_block;

buddy_block *free_lists[MAX_ORDER + 1]; // 每个列表头节点指向一个特定大小的空闲块链表

void init_buddy_system()
{ 

  void *base = (void *)0x87800000L;
  char *base_space = (void *)0x87000000L;
  /* int total_memory = 8 * 1024 * 1024; // 8MB */
  /* int block_size = total_memory;  */     // 最初只有一个8M的块

  // 初始化空闲列表
  for (int i = 0; i <= MAX_ORDER; i++)
  {
    free_lists[i] = NULL;
  }

  // 将8M块添加到空闲列表
  buddy_block *block = (buddy_block *)base;
  block->space=base_space;
  block->order = MAX_ORDER;
  block->next = NULL;
  free_lists[MAX_ORDER] = block;
}

void *buddy_alloc(int order)
{
  printf("------------<BUDDY-ALLOC-BEGIN>------------\n");
  if (order > MAX_ORDER)
    return NULL; // 请求大小超出最大限制
  if (order < 5){
    printf("Too tiny! Must lager then 24 Bytes.\n");
    return NULL; // 请求大小超出最小限制
  }
  // 找到第一个足够大的空闲块
  int current_order = order;
  while (current_order <= MAX_ORDER && free_lists[current_order] == NULL)
  {
    current_order++;
  }
  if (current_order > MAX_ORDER){
    printf("running out of the memory\n");
    return NULL; }// 没有足够的空闲块
  // 从空闲列表中移除块
  buddy_block *block = free_lists[current_order];
  free_lists[current_order] = block->next;
  if(current_order <= order){
    printf("Find proper size of buddy exist!\n");
  }
  // 如果块比需要的大，则拆分
  while (current_order > order)
  {
    current_order--;
    int new_block_size = 1 << current_order;
    buddy_block *new_block = (buddy_block *)((char *)block + new_block_size);
    new_block->order = current_order;
    new_block->space = (char*)((uintptr_t)new_block-BIAS);//涉及指针加减法
    new_block->next = free_lists[current_order];
    free_lists[current_order] = new_block;
    printf("block-address:%p,  generated blocks:%p, generated blocks'size %d\n",block->space,new_block,new_block->order);
  }
  printf("-------------<BUDDY-ALLOC-DONE>------------\n");
  return block->space;
}

void buddy_free(void *block_space, int order)
{
  printf("\n\n-------------<BUDDY-FREE-BEGIN>------------\n");
  if (order > MAX_ORDER || block_space == NULL)
    return;
  void* block=(void*)((uintptr_t)block_space+BIAS);
  uintptr_t block_space_addr = (uintptr_t)block_space;
  uintptr_t block_addr = (uintptr_t)block;
  uintptr_t base_addr = 0x87000000L;         // 基地址
  uintptr_t offset = block_space_addr - base_addr; // 计算相对于基地址的偏移
  uintptr_t buddy_offset;
  buddy_block *buddy;
  while (order <= MAX_ORDER)
  {
    uintptr_t block_size = 1 << (order); // 当前块大小
    buddy_offset = offset ^ block_size;  // 计算伙伴的偏移
    buddy = (buddy_block *)(block_addr + sizeof(struct buddy_block));
    buddy->space=(char*)(base_addr+buddy_offset);
    ///////////很妙////////////
    // 检查伙伴是否在同一级别的空闲列表中
    buddy_block *prev = NULL;
    buddy_block *current = free_lists[order];
    while (current != NULL)
    {
      if (current == buddy)
      {
        // 找到伙伴在空闲列表中，需要从链表中移除伙伴
        if (prev == NULL)
        {
          // 伙伴块是链表的第一个节点
          free_lists[order] = current->next;
        }
        else
        {
          // 伙伴块不是第一个节点，跳过伙伴块
          prev->next = current->next;
        }
        offset = offset & buddy_offset;
        block = (void *)(base_addr + offset);
        break; // 成功找到并移除伙伴，退出循环
      }
      prev = current;  
      current = current->next; // 移动current到下一个节点
    }

    if (current == NULL)
    {
      // 伙伴不在空闲列表中，无法合并
      // 将当前块添加到空闲列表并返回
      ((buddy_block *)block)->order = order;
      ((buddy_block *)block)->next = free_lists[order];
      free_lists[order] = (buddy_block *)block;
      /* memset((void*)((uintptr_t)block-BIAS), 0,block_size); */
      return;
    }

    // 如果可以合并，将两个块合并为一个更大的块
    // 计算合并后的块的新偏移和地址

    order++; // 移动到下一个更大的级别
    printf("-------------<BUDDY-FREE-DONE>------------\n\n");
  }

  // 如果循环结束没有返回，将合并后的最大块添加到空闲列表
  ((buddy_block *)block)->order = order;
  ((buddy_block *)block)->next = free_lists[order];
  free_lists[order] = (buddy_block *)block;
}








//////////////////////////////////

static int TOTALPAGES = 0;
void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem;

void kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
  init_buddy_system();
}

void freerange(void *pa_start, void *pa_end)
{
  int num = 0;
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  {
    num++;
    if (p + PGSIZE >= (char *)0x88000000L || p + PGSIZE < (char *)0x87000000L)
    {
      kfree(p);
      TOTALPAGES++;
    }
    else{
        memset(p, 0, PGSIZE);
    }
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 0, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

/////////////////////////////////////////////////////////

struct
    meminfo
{
  uint free_pages;  // 可用页数
  uint total_pages; // 系统总页数
  uint used_pages;  // 已使用页数
} info;

void *
test_free(void)
{
  // 初始化统计信息
  info.free_pages = 0;
  info.total_pages = TOTALPAGES; // 假定你已经有一个宏或常量定义了总页数
  info.used_pages = 0;
  // 计算空闲页数
  struct run *r;
  acquire(&kmem.lock);
  for (r = kmem.freelist; r; r = r->next)
  {
    info.free_pages++;
  }
  release(&kmem.lock);
  // 计算已使用页数
  info.used_pages = info.total_pages - info.free_pages;
  printf("TOTAL Pages: %d\n", TOTALPAGES);
  printf("Used Pages: %d\n", info.used_pages);
  printf("Free Pages: %d\n", info.free_pages);

printf("------------------------------Buddy-system test-------------------------------\n");
    printf("-------------First_buddy_alloc_test------------\n");
    char* ptr = (char*)buddy_alloc(5);
    if (ptr) {
        memmove(ptr, "ABC", 4); // 注意："ABC" 包含隐含的 '\0' 终止字符
        printf("space-address:%p, content:%s\n\n", (void*)ptr, ptr);
    }

    printf("-------------Second_buddy_alloc_test------------\n");
    char* ptr1 = (char*)buddy_alloc(8);
    if (ptr1) {
        memmove(ptr1, "Hello", 6); // 包含 '\0' 终止字符
        printf("space-address:%p, content:%s\n\n", (void*)ptr1, ptr1);
    }

    printf("-------------Third_buddy_alloc_test------------\n");
    char* ptr2 = (char*)buddy_alloc(16);
    if (ptr2) {
        memmove(ptr2, "World", 6); // 包含 '\0' 终止字符
        printf("space-address:%p, content:%s\n\n", (void*)ptr2, ptr2);
    }

    printf("-------------First_buddy_free_test-------------\n");
    buddy_free(ptr1, 8); // 注意：释放内存后立即访问是未定义行为
    // 尝试访问已释放内存（未定义行为，仅作为测试）
    printf("After free, ptr1 address: %p, content:%s--not been freshed", (void*)ptr1,ptr1);

    // 释放其它内存块
    buddy_free(ptr, 5);
    printf("After free, ptr2 address: %p, content:%s--not been freshed", (void*)ptr2,ptr2);

    buddy_free(ptr2, 16);
    printf("After free, ptr address: %p, content:%s--not been freshed", (void*)ptr,ptr);



printf("\n\n------------------------------Slab-system test-------------------------------\n");
    printf("-------------First_kmalloc_test------------\n");
    void* ptr3=kmalloc(512, 1); 
    if(ptr3)
    printf("kmalloc return space address:%p\n",ptr3); 

    printf("-------------Second_kmalloc_test------------\n");
    void* ptr4=kmalloc(1024, 1); 
    if(ptr4)
    printf("kmalloc return space address:%p\n",ptr4); 

  return (void *)&info;
}
