



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define	SLAB_NO_REAP		0x00001000UL	/* never reap from the cache */
#define	STATS		0
#define	DEBUG		0
#define BOOT_CPUCACHE_ENTRIES	1
#define smp_processor_id()			0
typedef unsigned short kmem_bufctl_t;


#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST3_INIT(parent) \
	{ \
		.slabs_full	= LIST_HEAD_INIT(parent.slabs_full), \
		.slabs_partial	= LIST_HEAD_INIT(parent.slabs_partial), \
		.slabs_free	= LIST_HEAD_INIT(parent.slabs_free) \
	}


//////* 双向链表 *//////
struct list_head {
    struct list_head *next, *prev;
};

static void list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}
///////////////////////



struct array_cache {
	unsigned int avail;
	unsigned int limit;
	unsigned int batchcount;
	unsigned int touched;
};



struct kmem_list3 {
	struct list_head	slabs_partial;	/* partial list first, better asm code */
	struct list_head	slabs_full;
	struct list_head	slabs_free;
	unsigned long	free_objects;
	int		free_touched;
	unsigned long	next_reap;
	struct array_cache	*shared;
};



typedef struct { volatile int counter; } atomic_t;

static struct list_head cache_chain;



struct arraycache_init {
	struct array_cache cache;
	void * entries[BOOT_CPUCACHE_ENTRIES];
};

static struct arraycache_init initarray_cache ={
	 	 .cache={ 
			.avail=0, 
			.limit=BOOT_CPUCACHE_ENTRIES, 
			.batchcount=1, 
			.touched=0} 
		 };




struct kmem_cache_s {
/* 1) per-cpu data, touched during every alloc/free */
	struct array_cache	*array[NCPU];
	unsigned int		batchcount;
	unsigned int		limit;
/* 2) touched by every alloc & free from the backend */
	struct kmem_list3	lists;
	/* NUMA: kmem_3list_t	*nodelists[MAX_NUMNODES] */
	unsigned int		objsize;
	unsigned int	 	flags;	/* constant flags */
	unsigned int		num;	/* # of objs per slab */
	unsigned int		free_limit; /* upper limit of objects in the lists */
    struct spinlock spinlock;
/* 	spinlock_t		spinlock; */

/* 3) cache_grow/shrink */
	/* order of pgs per slab (2^n) */
	unsigned int		gfporder;

	/* force GFP flags, e.g. GFP_DMA */
	unsigned int		gfpflags;

	size_t			    colour;		/* cache colouring range */
	unsigned int		colour_off;	/* colour offset */
	unsigned int		colour_next;	/* cache colouring */
	kmem_cache_t		*slabp_cache;
	unsigned int		slab_size;
	unsigned int		dflags;		/* dynamic flags */

	/* constructor func */
	void (*ctor)(void *, kmem_cache_t *, unsigned long);

	/* de-constructor func */
	void (*dtor)(void *, kmem_cache_t *, unsigned long);

/* 4) cache creation/removal */
	const char		*name;
	struct list_head	next;

/* 5) statistics */
#if STATS
	unsigned long		num_active;
	unsigned long		num_allocations;
	unsigned long		high_mark;
	unsigned long		grown;
	unsigned long		reaped;
	unsigned long 		errors;
	unsigned long		max_freeable;
	unsigned long		node_allocs;
	atomic_t		allochit;
	atomic_t		allocmiss;
	atomic_t		freehit;
	atomic_t		freemiss;
#endif
#if DEBUG
	int			dbghead;
	int			reallen;
#endif
};




typedef struct kmem_cache_s kmem_cache_t;
static kmem_cache_t cache_cache = {
	.lists		= LIST3_INIT(cache_cache.lists),
	.batchcount	= 1,
	.limit		= BOOT_CPUCACHE_ENTRIES,
	.objsize	= sizeof(kmem_cache_t),
	.flags		= SLAB_NO_REAP,
	.name		= "kmem_cache",
#if DEBUG
	.reallen	= sizeof(kmem_cache_t),
#endif
};

////////slab///////

struct slab {
	struct list_head	list;
	unsigned long		colouroff;
	void			*s_mem;		/* including colour offset */
	unsigned int		inuse;		/* num of objs active in slab */
	kmem_bufctl_t		free;
};

///////////////////

////*对齐*/////
#define L1_CACHE_BYTES		8
#define cache_line_size()	L1_CACHE_BYTES
#define ALIGN(val,align)	(((val) + ((align) - 1)) & ~((align) - 1))
//////////////

//////cache_size/////
struct cache_sizes {
	size_t		 cs_size;
	kmem_cache_t	*cs_cachep;
	kmem_cache_t	*cs_dmacachep;
};
struct cache_sizes malloc_sizes[] = {
#define CACHE(x) { .cs_size = (x) },
#include "kmalloc_sizes.h"
	{ 0, }
#undef CACHE
};
//////////////////////

////////CACHE_NAME////////
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
///////////////////

/////////////////////
#define CFLGS_OFF_SLAB		(0x80000000UL)
#define	OFF_SLAB(x)	((x)->flags & CFLGS_OFF_SLAB)
////////////////////
///////////////////////////////////////////////////////////函数/////////////////////////////////////////////////////////////////


/////* Cal the num objs, wastage, and bytes left over for a given slab size. */////
static void cache_estimate (unsigned long gfporder, size_t size, size_t align,
		 int flags, size_t *left_over, unsigned int *num)
{
	int i;
	unsigned int wastage = PAGE_SIZE<<gfporder;
	unsigned int extra = 0;
	unsigned int base = 0;

	if (!(flags & CFLGS_OFF_SLAB)) {
		base = sizeof(struct slab);
		extra = sizeof(kmem_bufctl_t);
	}
	i = 0;
	while (i*size + ALIGN(base+i*extra, align) <= wastage)
		i++;
	if (i > 0)
		i--;

	if (i > SLAB_LIMIT)
		i = SLAB_LIMIT;

	*num = i;
	wastage -= i*size;
	wastage -= ALIGN(base+i*extra, align);
	*left_over = wastage;
}


void  kmem_cache_init(void)
{
	size_t left_over;
	struct cache_sizes *sizes;
	struct cache_names *names;

	/*
	 * Fragmentation resistance on low memory - only use bigger
	 * page orders on machines with more than 32MB of memory.
	 */
	if (num_physpages > (32 << 20) >> PAGE_SHIFT)
		slab_break_gfp_order = BREAK_GFP_ORDER_HI;

	
	/* Bootstrap is tricky, because several objects are allocated
	 * from caches that do not exist yet:
	 * 1) initialize the cache_cache cache: it contains the kmem_cache_t
	 *    structures of all caches, except cache_cache itself: cache_cache
	 *    is statically allocated.
	 *    Initially an __init data area is used for the head array, it's
	 *    replaced with a kmalloc allocated array at the end of the bootstrap.
	 * 2) Create the first kmalloc cache.
	 *    The kmem_cache_t for the new cache is allocated normally. An __init
	 *    data area is used for the head array.
	 * 3) Create the remaining kmalloc caches, with minimally sized head arrays.
	 * 4) Replace the __init data head arrays for cache_cache and the first
	 *    kmalloc cache with kmalloc allocated arrays.
	 * 5) Resize the head arrays of the kmalloc caches to their final sizes.
	 */

	/* 1) create the cache_cache */
	initlock(&kmem_cache_t->lock, "cache_lock"); //Y
	INIT_LIST_HEAD(&cache_chain); //Y
	list_add(&cache_cache.next, &cache_chain,cache_chain.next);
	/* cache_cache.colour_off = cache_line_size(); */
	cache_cache.array[smp_processor_id()] = &initarray_cache.cache;

	cache_cache.objsize = ALIGN(cache_cache.objsize, cache_line_size());

	cache_estimate(0, cache_cache.objsize, cache_line_size(), 0,
				&left_over, &cache_cache.num);
	if (!cache_cache.num)
		printk("kernel BUG \n");

	cache_cache.colour = left_over/cache_cache.colour_off;
	cache_cache.colour_next = 0;
	cache_cache.slab_size = ALIGN(cache_cache.num*sizeof(kmem_bufctl_t) +
				sizeof(struct slab), cache_line_size());

	/* 2+3) create the kmalloc caches */
	sizes = malloc_sizes;
	names = cache_names;

	while (sizes->cs_size) {
		/* For performance, all the general caches are L1 aligned.
		 * This should be particularly beneficial on SMP boxes, as it
		 * eliminates "false sharing".
		 * Note for systems short on memory removing the alignment will
		 * allow tighter packing of the smaller caches. */
		sizes->cs_cachep = kmem_cache_create(names->name,
			sizes->cs_size, ARCH_KMALLOC_MINALIGN,
			(ARCH_KMALLOC_FLAGS | SLAB_PANIC), NULL, NULL);

		/* Inc off-slab bufctl limit until the ceiling is hit. */
		if (!(OFF_SLAB(sizes->cs_cachep))) {
			offslab_limit = sizes->cs_size-sizeof(struct slab);
			offslab_limit /= sizeof(kmem_bufctl_t);
		}

		sizes->cs_dmacachep = kmem_cache_create(names->name_dma,
			sizes->cs_size, ARCH_KMALLOC_MINALIGN,
			(ARCH_KMALLOC_FLAGS | SLAB_CACHE_DMA | SLAB_PANIC),
			NULL, NULL);

		sizes++;
		names++;
	}
	/* 4) Replace the bootstrap head arrays */
	{
		void * ptr;
		
		ptr = kmalloc(sizeof(struct arraycache_init), GFP_KERNEL);
		local_irq_disable();
		BUG_ON(ac_data(&cache_cache) != &initarray_cache.cache);
		memcpy(ptr, ac_data(&cache_cache), sizeof(struct arraycache_init));
		cache_cache.array[smp_processor_id()] = ptr;
		local_irq_enable();
	
		ptr = kmalloc(sizeof(struct arraycache_init), GFP_KERNEL);
		local_irq_disable();
		BUG_ON(ac_data(malloc_sizes[0].cs_cachep) != &initarray_generic.cache);
		memcpy(ptr, ac_data(malloc_sizes[0].cs_cachep),
				sizeof(struct arraycache_init));
		malloc_sizes[0].cs_cachep->array[smp_processor_id()] = ptr;
		local_irq_enable();
	}

	/* 5) resize the head arrays to their final sizes */
	{
		kmem_cache_t *cachep;
		down(&cache_chain_sem);
		list_for_each_entry(cachep, &cache_chain, next)
			enable_cpucache(cachep);
		up(&cache_chain_sem);
	}

	/* Done! */
	g_cpucache_up = FULL;

	/* Register a cpu startup notifier callback
	 * that initializes ac_data for all new cpus
	 */
	register_cpu_notifier(&cpucache_notifier);
	

	/* The reap timers are started later, with a module init call:
	 * That part of the kernel is not yet operational.
	 */
}


//////////////////////cache_create///////////////////////////
kmem_cache_t *
kmem_cache_create (const char *name, size_t size, size_t align,
	unsigned long flags, void (*ctor)(void*, kmem_cache_t *, unsigned long),
	void (*dtor)(void*, kmem_cache_t *, unsigned long))
{
	size_t left_over, slab_size, ralign;
	kmem_cache_t *cachep = NULL;

	/*
	 * Sanity checks... these are all serious usage bugs.
	 */
	if ((!name) ||
		in_interrupt() ||
		(size < BYTES_PER_WORD) ||
		(size > (1<<MAX_OBJ_ORDER)*PAGE_SIZE) ||
		(dtor && !ctor)) {
			printk(KERN_ERR "%s: Early error in slab %s\n",
					__FUNCTION__, name);
			BUG();
		}


	if (flags & SLAB_DESTROY_BY_RCU)
		BUG_ON(dtor);

	/*
	 * Always checks flags, a caller might be expecting debug
	 * support which isn't available.
	 */
	if (flags & ~CREATE_MASK)
		BUG();

	/* Check that size is in terms of words.  This is needed to avoid
	 * unaligned accesses for some archs when redzoning is used, and makes
	 * sure any on-slab bufctl's are also correctly aligned.
	 */
	if (size & (BYTES_PER_WORD-1)) {
		size += (BYTES_PER_WORD-1);
		size &= ~(BYTES_PER_WORD-1);
	}

	/* calculate out the final buffer alignment: */
	/* 1) arch recommendation: can be overridden for debug */
	if (flags & SLAB_HWCACHE_ALIGN) {
		/* Default alignment: as specified by the arch code.
		 * Except if an object is really small, then squeeze multiple
		 * objects into one cacheline.
		 */
		ralign = cache_line_size();
		while (size <= ralign/2)
			ralign /= 2;
	} else {
		ralign = BYTES_PER_WORD;
	}
	/* 2) arch mandated alignment: disables debug if necessary */
	if (ralign < ARCH_SLAB_MINALIGN) {
		ralign = ARCH_SLAB_MINALIGN;
		if (ralign > BYTES_PER_WORD)
			flags &= ~(SLAB_RED_ZONE|SLAB_STORE_USER);
	}
	/* 3) caller mandated alignment: disables debug if necessary */
	if (ralign < align) {
		ralign = align;
		if (ralign > BYTES_PER_WORD)
			flags &= ~(SLAB_RED_ZONE|SLAB_STORE_USER);
	}
	/* 4) Store it. Note that the debug code below can reduce
	 *    the alignment to BYTES_PER_WORD.
	 */
	align = ralign;

	/* Get cache's description obj. */
	cachep = (kmem_cache_t *) kmem_cache_alloc(&cache_cache, SLAB_KERNEL);
	if (!cachep)
		goto opps;
	memset(cachep, 0, sizeof(kmem_cache_t));

	/* Determine if the slab management is 'on' or 'off' slab. */
	if (size >= (PAGE_SIZE>>3))
		/*
		 * Size is large, assume best to place the slab management obj
		 * off-slab (should allow better packing of objs).
		 */
		flags |= CFLGS_OFF_SLAB;

	size = ALIGN(size, align);

	if ((flags & SLAB_RECLAIM_ACCOUNT) && size <= PAGE_SIZE) {
		/*
		 * A VFS-reclaimable slab tends to have most allocations
		 * as GFP_NOFS and we really don't want to have to be allocating
		 * higher-order pages when we are unable to shrink dcache.
		 */
		cachep->gfporder = 0;
		cache_estimate(cachep->gfporder, size, align, flags,
					&left_over, &cachep->num);
	} else {
		/*
		 * Calculate size (in pages) of slabs, and the num of objs per
		 * slab.  This could be made much more intelligent.  For now,
		 * try to avoid using high page-orders for slabs.  When the
		 * gfp() funcs are more friendly towards high-order requests,
		 * this should be changed.
		 */
		do {
			unsigned int break_flag = 0;
cal_wastage:
			cache_estimate(cachep->gfporder, size, align, flags,
						&left_over, &cachep->num);
			if (break_flag)
				break;
			if (cachep->gfporder >= MAX_GFP_ORDER)
				break;
			if (!cachep->num)
				goto next;
			if (flags & CFLGS_OFF_SLAB &&
					cachep->num > offslab_limit) {
				/* This num of objs will cause problems. */
				cachep->gfporder--;
				break_flag++;
				goto cal_wastage;
			}

			/*
			 * Large num of objs is good, but v. large slabs are
			 * currently bad for the gfp()s.
			 */
			if (cachep->gfporder >= slab_break_gfp_order)
				break;

			if ((left_over*8) <= (PAGE_SIZE<<cachep->gfporder))
				break;	/* Acceptable internal fragmentation. */
next:
			cachep->gfporder++;
		} while (1);
	}

	if (!cachep->num) {
		printk("kmem_cache_create: couldn't create cache %s.\n", name);
		kmem_cache_free(&cache_cache, cachep);
		cachep = NULL;
		goto opps;
	}
	slab_size = ALIGN(cachep->num*sizeof(kmem_bufctl_t)
				+ sizeof(struct slab), align);

	/*
	 * If the slab has been placed off-slab, and we have enough space then
	 * move it on-slab. This is at the expense of any extra colouring.
	 */
	if (flags & CFLGS_OFF_SLAB && left_over >= slab_size) {
		flags &= ~CFLGS_OFF_SLAB;
		left_over -= slab_size;
	}

	if (flags & CFLGS_OFF_SLAB) {
		/* really off slab. No need for manual alignment */
		slab_size = cachep->num*sizeof(kmem_bufctl_t)+sizeof(struct slab);
	}

	cachep->colour_off = cache_line_size();
	/* Offset must be a multiple of the alignment. */
	if (cachep->colour_off < align)
		cachep->colour_off = align;
	cachep->colour = left_over/cachep->colour_off;
	cachep->slab_size = slab_size;
	cachep->flags = flags;
	cachep->gfpflags = 0;
	if (flags & SLAB_CACHE_DMA)
		cachep->gfpflags |= GFP_DMA;
	spin_lock_init(&cachep->spinlock);
	cachep->objsize = size;
	/* NUMA */
	INIT_LIST_HEAD(&cachep->lists.slabs_full);
	INIT_LIST_HEAD(&cachep->lists.slabs_partial);
	INIT_LIST_HEAD(&cachep->lists.slabs_free);

	if (flags & CFLGS_OFF_SLAB)
		cachep->slabp_cache = kmem_find_general_cachep(slab_size,0);
	cachep->ctor = ctor;
	cachep->dtor = dtor;
	cachep->name = name;

	/* Don't let CPUs to come and go */
	lock_cpu_hotplug();

	if (g_cpucache_up == FULL) {
		enable_cpucache(cachep);
	} else {
		if (g_cpucache_up == NONE) {
			/* Note: the first kmem_cache_create must create
			 * the cache that's used by kmalloc(24), otherwise
			 * the creation of further caches will BUG().
			 */
			cachep->array[smp_processor_id()] = &initarray_generic.cache;
			g_cpucache_up = PARTIAL;
		} else {
			cachep->array[smp_processor_id()] = kmalloc(sizeof(struct arraycache_init),GFP_KERNEL);
		}
		BUG_ON(!ac_data(cachep));
		ac_data(cachep)->avail = 0;
		ac_data(cachep)->limit = BOOT_CPUCACHE_ENTRIES;
		ac_data(cachep)->batchcount = 1;
		ac_data(cachep)->touched = 0;
		cachep->batchcount = 1;
		cachep->limit = BOOT_CPUCACHE_ENTRIES;
		cachep->free_limit = (1+num_online_cpus())*cachep->batchcount
					+ cachep->num;
	} 

	cachep->lists.next_reap = jiffies + REAPTIMEOUT_LIST3 +
					((unsigned long)cachep)%REAPTIMEOUT_LIST3;

	/* Need the semaphore to access the chain. */
	down(&cache_chain_sem);
	{
		struct list_head *p;
		mm_segment_t old_fs;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		list_for_each(p, &cache_chain) {
			kmem_cache_t *pc = list_entry(p, kmem_cache_t, next);
			char tmp;
			/* This happens when the module gets unloaded and doesn't
			   destroy its slab cache and noone else reuses the vmalloc
			   area of the module. Print a warning. */
			if (__get_user(tmp,pc->name)) { 
				printk("SLAB: cache with size %d has lost its name\n", 
					pc->objsize); 
				continue; 
			} 	
			if (!strcmp(pc->name,name)) { 
				printk("kmem_cache_create: duplicate cache %s\n",name); 
				up(&cache_chain_sem); 
				unlock_cpu_hotplug();
				BUG(); 
			}	
		}
		set_fs(old_fs);
	}

	/* cache setup completed, link it into the list */
	list_add(&cachep->next, &cache_chain);
	up(&cache_chain_sem);
	unlock_cpu_hotplug();
opps:
	if (!cachep && (flags & SLAB_PANIC))
		panic("kmem_cache_create(): failed to create slab `%s'\n",
			name);
	return cachep;
}
EXPORT_SYMBOL(kmem_cache_create);

#if DEBUG
static void check_irq_off(void)
{
	BUG_ON(!irqs_disabled());
}

static void check_irq_on(void)
{
	BUG_ON(irqs_disabled());
}

static void check_spinlock_acquired(kmem_cache_t *cachep)
{
#ifdef CONFIG_SMP
	check_irq_off();
	BUG_ON(spin_trylock(&cachep->spinlock));
#endif
}
#else
#define check_irq_off()	do { } while(0)
#define check_irq_on()	do { } while(0)
#define check_spinlock_acquired(x) do { } while(0)
#endif
