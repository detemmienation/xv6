#include <stddef.h>

#define CACHE_NAME_MAX 16
#define SLAB_OBJ_MAX 64  // Assume each Slab contains at most 64 objects

typedef unsigned short kmem_bufctl_t;

#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define container_of(ptr, type, member) ({          \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})


struct list_head {
    struct list_head *next, *prev;
};



struct slab {
	struct list_head	list;
	unsigned long		colouroff;
	void			    *s_mem;		/* including colour offset */
	unsigned int		inuse;		/* num of objs active in slab */
	kmem_bufctl_t		free;
    void *              objs;
};



struct cache_node {
    struct list_head slabs_partial; /* partial list first, better asm code */
    struct list_head slabs_full;
    struct list_head slabs_free;
    unsigned long num_slabs;
    unsigned long free_objects;
    //unsigned int free_limit;
    //unsigned int colour_next;   /* Per-node cache coloring */
    struct array_cache *shared; /* shared per node */
    //struct alien_cache **alien; /* on other nodes */
    //unsigned long next_reap;    /* updated without locking */
    int free_touched;       /* updated without locking */
    
};


// First define the structure 'cache'
struct cache {
    struct array_cache *array[NCPU];
    unsigned int		batchcount;
    const char        *name;
    struct list_head  next;
    struct cache_node mem_cache_node;


    unsigned int		slab_size;
    unsigned int        num;        // Number of objs per slab
    struct spinlock     lock;
    int                 obj_size;   // Size of each object
    int                 order;
    
    void (*ctor)(void *, struct cache *, unsigned long);
    void (*dtor)(void *, struct cache *, unsigned long);
};

typedef struct cache  cache_t;





/* 
成员名称	             类型	                             描述
cpu_cache	            struct array_cache __percpu*	    指向每个CPU专有的数组缓存的指针，用于提高每个处理器在访问缓存时的性能。
batchcount	            unsigned int	                    一次性从中心缓存向CPU局部缓存转移的对象数量，用于调节性能与内存使用。
limit	                unsigned int	                    缓存中对象的最大数量限制。
shared	                unsigned int	                    决定多少个CPU可以共享同一组缓存。
size	                unsigned int	                    单个对象的大小。
reciprocal_buffer_size	struct reciprocal_value	            用于快速计算缓冲区大小的倒数，优化计算效率。
flags	                unsigned int	                    缓存的属性标志，例如是否启用调试。
num	                    unsigned int	                    每个slab中的对象数量。
gfporder	            unsigned int	                    分配slab时请求的页的阶数（以2的幂表示）。
allocflags	            gfp_t	                            在分配缓存时强制使用的分配标志，例如GFP_DMA用于DMA内存分配。
colour	                size_t	                            缓存颜色变化的范围，用于内存对齐和减少缓存行冲突。
colour_off	            unsigned int	                    缓存颜色的偏移量，与colour配合使用以实现不同的内存对齐方案。
freelist_cache	        struct kmem_cache*	                用于管理空闲对象链表的缓存。
freelist_size	        unsigned int	                    空闲链表所占的内存大小。
ctor	                void (*)(void *obj)	                指向构造函数的指针，当新对象被创建时调用此函数进行初始化。
name	                const char*	                        缓存的名称。
list	                struct list_head	                缓存链表，用于将多个缓存连接在一起。
refcount	            int	                                缓存的引用计数，用于管理缓存的生命周期。
object_size	            int	                                用户请求的对象大小，可能与size不同，因为size可能包含额外的内核元数据。
align	                int	                                对象的对齐要求。
node	                struct kmem_cache_node*[MAX_NUMNODES]	指向所有节点（NUMA架构中的内存节点）的kmem_cache_node结构的指针数组。
 */





/* 
成员名称	     类型	                 描述
list_lock	    spinlock_t	            用于保护结构体内部列表在并发访问时的自旋锁。
slabs_partial	struct list_head	    指向部分分配的slabs的链表头，这些slabs已经被部分分配了对象。
slabs_full	    struct list_head	    指向完全分配的slabs的链表头，这些slabs的所有对象都已经被分配。
slabs_free  	struct list_head	    指向完全未分配的slabs的链表头，这些slabs中的对象都还未被分配。
num_slabs	    unsigned long	        表示当前节点拥有的slabs总数。
free_objects	unsigned long	        表示在这个节点上所有空闲的对象总数。
free_limit	    unsigned int	        控制这个节点上可以拥有的空闲对象的最大数量的阈值。
colour_next	    unsigned int        	用于实现缓存颜色变化，帮助减少缓存行冲突的内存对齐技术。
shared	        struct array_cache*	    指向共享数组缓存的指针，为这个节点上所有CPU的共享资源，用以提高内存分配效率。
alien	        struct alien_cache**	指向存储来自其他节点的缓存行的指针数组，有助于处理不同CPU节点间的内存分配。
next_reap	    unsigned long	        决定下一次回收操作的时间戳。回收操作是清理内存中不再需要的对象的过程。
free_touched	int	                    记录自上次回收以来是否有对象被释放，用于决定是否需要执行新的回收操作。 */






/* 
成员名称	 类型	         描述
avail	    unsigned int	表示当前缓存中可用对象的数量。这些对象可以直接被分配，无需从通用内存池中分配。
limit	    unsigned int	缓存能够容纳的对象的最大数量。这个限制确保缓存不会无限制地增长，从而占用过多的内存。
batchcount	unsigned int	一次性从中心缓存向局部缓存（即这个结构体表示的缓存）转移的对象数量。这个数值用于平衡性能与内存使用之间的关系。
touched	    unsigned int	上次被访问或更改的时间戳。这通常用于帮助决定哪些缓存最近没有被使用，并可能被清理以释放内存。
entry[]	    void*数组	    指向实际存储的对象的指针数组。这是一个灵活数组成员，其大小在运行时决定，这样可以动态地存储不同数量的对象指针。数组中的每个元素都是指向某种类型的对象的指针。
 */
struct array_cache {
    unsigned int avail;
    unsigned int limit;
    unsigned int batchcount;
    unsigned int touched;
    void *entry[];  /*
             * Must have this definition in here for the proper
             * alignment of array_cache. Also simplifies accessing
             * the entries.
             */
};
