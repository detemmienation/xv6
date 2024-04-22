#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "cache.h"
#include "proc.h"


#define cache_line_size() 8
#define BUFCTL_END	(((kmem_bufctl_t)(~0U))-0)
#define	SLAB_LIMIT	(((kmem_bufctl_t)(~0U))-2)
#define ALIGN(val,align)	(((val) + ((align) - 1)) & ~((align) - 1))
#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define CFLGS_OFF_SLAB		(0x80000000UL)
#define smp_processor_id()   0
#define BATCHREFILL_LIMIT	16
#define MAX_OBJ_NUM        	16

static enum {
	NONE,
	PARTIAL,
	FULL
} g_cpucache_up;


#define LIST_INIT(parent) \
	{ \
		.slabs_full	= LIST_HEAD_INIT(parent.slabs_full), \
		.slabs_partial	= LIST_HEAD_INIT(parent.slabs_partial), \
		.slabs_free	= LIST_HEAD_INIT(parent.slabs_free) \
	}

#define INIT_LIST_HEAD(ptr) do { \
    (ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)


static inline void * __cache_alloc (cache_t *cachep);
void * kmem_cache_alloc (cache_t *cachep);
struct cache_sizes {
	size_t			 cs_size;
	cache_t	*cs_cachep;
};
struct cache_sizes malloc_sizes[] = {
#define CACHE(x) { .cs_size = (x) },
#include "kmalloc_sizes.h"
	{ 0, }
#undef CACHE
};


struct cache_names {
	char *name;
	char *name_dma;
};
static struct cache_names cache_names[] = {
#define CACHE(x) { .name = "size-" #x, .name_dma = "size-" #x "(DMA)" },
#include "kmalloc_sizes.h"
	{ NULL, }
#undef CACHE
};

static inline kmem_bufctl_t *slab_bufctl(struct slab *slabp)
{
	return (kmem_bufctl_t *)(slabp+1);
};


static inline struct array_cache *ac_data(cache_t *cachep)
{
	return cachep->array[smp_processor_id()];
};

#define list_del(entry) do { \
	(entry)->prev->next = (entry)->next; \
	(entry)->next->prev = (entry)->prev; \
} while (0)

static void __list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
};

static inline void list_add(struct list_head *new, struct list_head *head)
{
	__list_add(new, head, head->next);
}



static struct list_head cache_chain;
static cache_t cache_cache= {
    .mem_cache_node=LIST_INIT(cache_cache.mem_cache_node),
    .name = "kmem_cache",
	.batchcount	= 1,
	.obj_size	= sizeof(cache_t),
	.order=12
};

static inline void ** ac_entry(struct array_cache *ac)
{
	return (void**)(ac+1);
};


static struct array_cache initarray_cache  =
	{   .avail=0,
    	.limit=1,
    	.batchcount=1,
    	.touched=0
	 };

static struct array_cache initarray_generic =
	{ 	.avail=0,
    	.limit=1,
    	.batchcount=1,
    	.touched=0
	 };

#define	BYTES_PER_WORD		sizeof(void *)

static void cache_estimate (unsigned long gfporder, unsigned int size, unsigned int align,
		 int flags, unsigned int *left_over, unsigned int *num)
         {
            int i;
			if(gfporder<12){
				gfporder=12;
			}
            unsigned int wastage=1<<gfporder;
            unsigned int extra =0;
            unsigned int base = 0;
            if(!(flags&CFLGS_OFF_SLAB)){   //检查对象是否在slab之外
                base=sizeof(struct slab);
            }
            i=0;
            while(i*size+ALIGN(base+i*extra,align)<=wastage){
                i++;}
            if(i>0)
                i--;
            if (i > SLAB_LIMIT)
		    i = SLAB_LIMIT;
            *num = i;
	        wastage -= i*size;
	        wastage -= ALIGN(base+i*extra, align);
	        *left_over = wastage;
         }


#define list_data(cachep) \
	(&(cachep)->mem_cache_node)


int
cache_grow(cache_t *cache)
{
  char *page;
  struct slab *newslab;
  page = buddy_alloc(cache->order); 

  if(page == 0){
	printf("cache grow fail\n");
    return 0;} // 内存申请失败
   
  newslab = (struct slab*)page;
  newslab->objs = page + sizeof(struct slab); // 分配对象空间
  newslab->inuse=0;
  newslab->free= ((1<<cache->order)-sizeof(struct slab))/cache->obj_size ;

  acquire(&cache->lock);
  list_add(&newslab->list,&list_data(cache)->slabs_free);
  release(&cache->lock);

  return 1; // 成功
}




static void* cache_alloc_refill(cache_t* cachep)
{
	int batchcount;
	struct cache_node *l3;
	struct array_cache *ac;

	ac = ac_data(cachep);
	
retry:
	batchcount = ac->batchcount;
	if (!ac->touched && batchcount > BATCHREFILL_LIMIT) {
		/* if there was little recent activity on this
		 * cache, then perform only a partial refill.
		 * Otherwise we could generate refill bouncing.
		 */
		batchcount = BATCHREFILL_LIMIT;
	}
	l3 = list_data(cachep);
	/* BUG_ON(ac->avail > 0); */
	acquire(&cachep->lock);
	
	/* if (l3->shared) {  share 初始化有问题
		printf("6\n\n");
		struct array_cache *shared_array = l3->shared;
		if (shared_array->avail) {
			if (batchcount > shared_array->avail)
				batchcount = shared_array->avail;
			shared_array->avail -= batchcount;
			ac->avail = batchcount;
			memmove(ac_entry(ac), &ac_entry(shared_array)[shared_array->avail], sizeof(void*)*batchcount);
			shared_array->touched = 1;
			goto alloc_done;
		}
	} */
	while (batchcount > 0) {
		struct list_head *entry;
		struct slab *slabp;
		/* Get slab alloc is to come from. */
		entry = l3->slabs_partial.next;
		if (entry == &l3->slabs_partial) {
			l3->free_touched = 1;
			entry = l3->slabs_free.next;
			if (entry == &l3->slabs_free){
				goto must_grow;}
		}
		slabp = list_entry(entry, struct slab, list);
		printf("slab-address:%p,slab->inuse:%d,cachep->num:%d,batchcount:%d,cachep->obj_size:%d\n",\
				slabp,slabp->inuse,cachep->num,batchcount,cachep->obj_size);
		if(slabp->inuse >=cachep->num){
			printf("\nReach the top limit of a cache\n");
			release(&cachep->lock);
			return NULL;
		}
		while (slabp->inuse < cachep->num && batchcount--) {
			 /* kmem_bufctl_t next;  */
			/* get obj pointer */
			ac_entry(ac)[ac->avail++]= &slabp->objs+(cachep->obj_size)*(slabp->free--)/sizeof(void *);
			printf("slabp->free:%d,nextobj_position:%p\n",slabp->free,ac_entry(ac)[ac->avail-1]);
			slabp->inuse++;
			/* next = slab_bufctl(slabp)[slabp->free]; */
#if DEBUG
			slab_bufctl(slabp)[slabp->free] = BUFCTL_FREE;
#endif
		    /* slabp->free = next; */ 
		}
		/* move slabp to correct slabp list: */
		list_del(&slabp->list);
		if (slabp->free == BUFCTL_END)
			list_add(&slabp->list, &l3->slabs_full);
		else
			list_add(&slabp->list, &l3->slabs_partial);
	}
	
must_grow:
	l3->free_objects -= ac->avail;
/* alloc_done: */
	release(&cachep->lock);

	if (/* unlikely */(!ac->avail)) {
		int x;
		
		x = cache_grow(cachep);
		
		// cache_grow can reenable interrupts, then ac could change.
		ac = ac_data(cachep);
		if (!x && ac->avail == 0)	// no objects in sight? abort
			return NULL;

		if (!ac->avail)		// objects refilled by interrupt?
			goto retry;
	}
	ac->touched = 1;
	return  ac_entry(ac)[--ac->avail];
}


static inline void * __cache_alloc (cache_t *cachep)
{
/* 	unsigned long save_flags; */
	void* objp;
	struct array_cache *ac;
/* 
	cache_alloc_debugcheck_before(cachep, flags); */
/* 
	local_irq_save(save_flags); */ //关闭中断
	ac = ac_data(cachep);
	if (/* likely */(ac->avail)) {
/* 		STATS_INC_ALLOCHIT(cachep); */
		printf("acquire obj from:cache-array.\n");
		ac->touched = 1;
		objp = ac_entry(ac)[--ac->avail];
		
	} else {
	/* 	STATS_INC_ALLOCMISS(cachep); */
		printf("acquire obj from:memcache-node.\n");
		objp = cache_alloc_refill(cachep);}
/* 	local_irq_restore(save_flags); */
/* 	objp = cache_alloc_debugcheck_after(cachep, flags, objp, __builtin_return_address(0));
 */		
return objp;
}

void * kmem_cache_alloc (cache_t *cachep)
{
	return __cache_alloc(cachep);
	
}



void init_cache(cache_t *cache,int size) {
    if (cache != NULL) {
		 unsigned int left_over;
        // 手动初始化链表头
        cache->mem_cache_node.slabs_full.next = &cache->mem_cache_node.slabs_full;
        cache->mem_cache_node.slabs_full.prev = &cache->mem_cache_node.slabs_full;

        cache->mem_cache_node.slabs_partial.next = &cache->mem_cache_node.slabs_partial;
        cache->mem_cache_node.slabs_partial.prev = &cache->mem_cache_node.slabs_partial;

        cache->mem_cache_node.slabs_free.next = &cache->mem_cache_node.slabs_free;
        cache->mem_cache_node.slabs_free.prev = &cache->mem_cache_node.slabs_free;

        // 直接赋值其他字段
        cache->name = "kmem_cache";
		INIT_LIST_HEAD(&cache->next);
        cache->batchcount = 1;
        cache->obj_size = size;
		int value=cache->obj_size*MAX_OBJ_NUM;
		int exponent = 0; // 2的0次方是1
    	while (value > 1) {
        	value >>= 1; // 将值右移一位
        	exponent++;  // 每右移一次，指数增加1
   		}
		cache->order=exponent;
		cache_estimate(cache->order, cache->obj_size, cache_line_size(), 0,
				&left_over, &cache->num);
		cache->array[0]=&initarray_generic;

    }
}



cache_t*
kmem_cache_create (const char *name, size_t size, size_t align,
	unsigned long flags, void (*ctor)(void*, cache_t *, unsigned long),
	void (*dtor)(void*, cache_t *, unsigned long))
    {
       /*  size_t left_over, slab_size, ralign; */
	    cache_t *cachep = NULL;
        if ((!name) ||
		(size < BYTES_PER_WORD) ||
		(size > (1<<8)*PGSIZE) ||
		(dtor && !ctor)) {
            printf("kmem_cache_create wrong");
		}
    /* Check that size is in terms of words.  This is needed to avoid
	 * unaligned accesses for some archs when redzoning is used, and makes
	 * sure any on-slab bufctl's are also correctly aligned.
	 */
	if (size & (BYTES_PER_WORD-1)) {
		size += (BYTES_PER_WORD-1);
		size &= ~(BYTES_PER_WORD-1);
	}

    cachep = (cache_t *) kmem_cache_alloc(&cache_cache);

	if (!cachep)
		return 0;/* goto opps */
	memset(cachep, 0, sizeof(cache_t));
	if (g_cpucache_up == FULL) {
		/* enable_cpucache(cachep); */
	} else {
		if (g_cpucache_up == NONE) {
			/* Note: the first kmem_cache_create must create
			 * the cache that's used by kmalloc(24), otherwise
			 * the creation of further caches will BUG().
			 */
			cachep->array[smp_processor_id()] = &initarray_generic;
			g_cpucache_up = PARTIAL;
		} else {
			cachep->array[smp_processor_id()] = buddy_alloc(14);
		}
		}
	//初始化malloc_size;
	ac_data(cachep)->avail=0;
	init_cache(cachep,size);





	return cachep;
    }



void cache_init(void) {
	printf("\n--------------------------------------------------\
	<KMEMCACHE-INIT-BEGIN>\
	--------------------------------------------------\n");
    unsigned int left_over;
	struct cache_sizes *sizes;
	struct cache_names *names;
    initlock(&cache_cache.lock, "cache_lock");
    INIT_LIST_HEAD(&cache_chain); \
    list_add(&cache_cache.next, &cache_chain);
    cache_cache.array[0]=&initarray_cache;
	cache_cache.obj_size = ALIGN(cache_cache.obj_size, 8);
    cache_estimate(0, cache_cache.obj_size, cache_line_size(), 0,
				&left_over, &cache_cache.num);
    if (!cache_cache.num) //计算请求的
		printf("kernel BUG \n");
   	cache_cache.slab_size = ALIGN(cache_cache.num*sizeof(kmem_bufctl_t) +
			sizeof(struct slab), cache_line_size());
	sizes = malloc_sizes;
	names = cache_names;
    while (sizes->cs_size) {
	printf("--------------------<MALLOC_CACHE-CREATE-BEGIN>----------------\n");
    sizes->cs_cachep = kmem_cache_create(names->name,
			sizes->cs_size, 0,
			0, NULL, NULL);
	sizes++;
	names++;
	printf("--------------------<MALLOC_CACHE-CREATE-DONE>-----------------\n\n");
	/* list_add(&sizes->cs_cachep->next, &cache_chain);
	printf("qqq:%p\n\n",&sizes->cs_cachep->next); */
    }
{
    void *ptr;

    // xv6 does not use GFP_KERNEL flag, and memory allocation is more straightforward
    ptr =buddy_alloc(16);/*  kmalloc(sizeof(struct array_cache));  */// kmalloc equivalent in xv6 could be kalloc()
    push_off(); // This is the xv6 way to disable interrupts, equivalent to local_irq_disable();

    // xv6 does not have BUG_ON macro, but you could use panic for fatal errors
    // Also, it does not support dynamic per-CPU structures, so this check might be irrelevant
    // panic("cache inconsistency"); if needed
    
    memmove(ptr, cache_cache.array[smp_processor_id()], sizeof(struct array_cache)); // Use memmove as it's safer than memcpy in general; also, array handling might be different
    // Directly manipulate array since there's no per-CPU structure in xv6
    cache_cache.array[smp_processor_id()] = ptr; // Since there's no SMP, we just use index 0
	
    pop_off(); // Re-enable interrupts, equivalent to local_irq_enable();
    
    // Repeat for the second allocation
    ptr =buddy_alloc(16); /* kmalloc(sizeof(struct array_cache)); */
    push_off();// Disable interrupts


    // Again, assuming this adaptation makes sense in the context of xv6
    memmove(ptr, ac_data(malloc_sizes[0].cs_cachep), sizeof(struct array_cache));

    malloc_sizes[0].cs_cachep->array[0] = ptr; // No SMP in xv6, so just index 0
    pop_off();  // Re-enable interrupts
}
printf("\n--------------------------------------------------\
<KMEMCACHE-INIT-DONE>\
--------------------------------------------------\n");
}

/* void* cache_alloc(cache_t* cache) {
    acquire(&cache->lock);

    // 遍历slabs寻找有空闲对象的Slab
    for (struct slab* s = cache->slabs; s; s = s->next) {
        if (s->free > 0) {
            // 在这个Slab中分配一个对象
            void* obj = s->objs[--s->free];
            release(&cache->lock);
            return obj;
        }
    }

    // 没有找到空闲对象，需要分配一个新的Slab
    struct slab* new_slab = alloc_slab(cache->obj_size);
    if (!new_slab) {
        release(&cache->lock);
        return 0;
    }
    // 假设新分配的Slab已经设置好了objs和free
    cache->slabs = new_slab;  // 将新Slab添加到链表头部

    void* obj = new_slab->objs[--new_slab->free];
    release(&cache->lock);
    return obj;
}
 */
void cache_free(cache_t* cache, void* obj) {
    // 此函数需要根据对象地址找到其所在的Slab，并将对象标记为可用
    // 这里省略了具体实现，实现这一功能需要对Slab和对象之间的关系进行管理
}











void * __kmalloc (size_t size, int flags)
{
	struct cache_sizes *csizep =&malloc_sizes[0];

	for (; csizep->cs_size; csizep++) {
		if (size > csizep->cs_size){
			continue;
		}
			
#if DEBUG
		/* This happens if someone tries to call
		 * kmem_cache_create(), or kmalloc(), before
		 * the generic caches are initialized.
		 */
		BUG_ON(csizep->cs_cachep == NULL);
#endif
		return __cache_alloc(csizep->cs_cachep);
	}
	return NULL;
}




void *kmalloc(size_t size, int flags)
{	
	if (__builtin_constant_p(size)) {
		int i = 0;
#define CACHE(x) \
		if (size <= x) \
			goto found; \
		else \
			i++;
#include "kmalloc_sizes.h"
#undef CACHE
		{
			extern void __you_cannot_kmalloc_that_much(void);
			__you_cannot_kmalloc_that_much();
		}
found:
		return kmem_cache_alloc(malloc_sizes[i].cs_cachep);
	}
	return __kmalloc(size, flags);
}


/* static inline void __cache_free (cache_t *cachep, void* objp)
{
	struct array_cache *ac = ac_data(cachep);

	check_irq_off();
	objp = cache_free_debugcheck(cachep, objp, __builtin_return_address(0));

	if (likely(ac->avail < ac->limit)) {
		STATS_INC_FREEHIT(cachep);
		ac_entry(ac)[ac->avail++] = objp;
		return;
	} else {
		STATS_INC_FREEMISS(cachep);
		cache_flusharray(cachep, ac);
		ac_entry(ac)[ac->avail++] = objp;
	}
}
 */


/* 
static struct kmem_cache kmem_cache_boot{

} */

//mm/slab.c
/*
 * Initialisation.  Called after the page allocator have been initialised and
 * before smp_init().
 *slab系统初始化时伙伴系统已经初始化,但在多处理器系统上，启动CPU此时正在运行, 而其他CPU尚未初始化.
 */











/* 
void devidemem(struct slab* s, char* mem,int obj_size) {
    int i = 0;
    char* page_end = mem + PGSIZE; // 计算当前页的结束地址
    for(; mem + obj_size < page_end; mem += obj_size) {
        s->objs[i] = mem;
        i++;
    }
} 


struct slab* alloc_slab(int obj_size) {
    struct slab* s = (struct slab*)kalloc(); // 假设kalloc能够分配足够的内存给slab管理结构本身
    if (s == NULL) return NULL;

    int objs_needed = SLAB_OBJ_MAX;
    // 分配所需数量的页面，直到达到SLAB_OBJ_MAX或分配失败
    for (int i = 0; i < SLAB_OBJ_MAX && objs_needed > 0; i++ ) {
        char* mem = kalloc(); // 分配一页内存
        if (mem == NULL) {
            // 分配失败，需要释放已分配的页并返回NULL
            // 这里留给读者作为练习：遍历s->obj数组，对非NULL项调用kfree
            for (int j = 0; j < i; j++) {
                if (s->objs[j] != NULL) {
                    kfree(s->objs[j]); // 释放已分配的内存
                    s->objs[j] = NULL;
                }
            }
            return NULL;
        }
        devidemem(s, mem, obj_size);
        objs_needed -= PGSIZE / obj_size;
        s->free=SLAB_OBJ_MAX;
    }
    return s; // 返回分配好的slab
}
 */