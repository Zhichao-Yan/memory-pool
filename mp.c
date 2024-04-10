#include "mp.h"

#define MP_CHUNK_HEAD sizeof(struct mp_chunk)
#define MP_CHUNK_POINTER sizeof(struct mp_chunk *)
#define MP_ALIGN_SIZE(size, alignment) ((((size_t)size) + (alignment - 1)) & ~(alignment - 1))

#define MP_LOCK(lockobj)                    \
    do                                      \
    {                                       \
        pthread_mutex_lock(&lockobj->lock); \
    } while (0)
#define MP_UNLOCK(lockobj)                    \
    do                                        \
    {                                         \
        pthread_mutex_unlock(&lockobj->lock); \
    } while (0)


#define INSERT_INTO_LINKLIST(head, x) \
    do                                \
    {                                 \
        x->prev = NULL;               \
        x->next = head;               \
        if (head)                     \
            head->prev = x;           \
        head = x;                     \
    } while (0)

#define DELETE_FROM_LINKLIST(head, x)    \
    do                                   \
    {                                    \
        if (!x->prev)                    \
        {                                \
            head = x->next;              \
            if (x->next)                 \
                x->next->prev = NULL;    \
        }                                \
        else                             \
        {                                \
            x->prev->next = x->next;     \
            if (x->next)                 \
                x->next->prev = x->prev; \
        }                                \
    } while (0)

/* 初始化一个内存块的头部 */
static void init_chunk_head(mp_chunk *ck,mem_size_t chunk_size)
{
    ck->is_free = 1;
    ck->next = NULL;
    ck->prev = NULL;
    ck->size = chunk_size;
    *(mp_chunk**)((char*)ck + ck->size - MP_CHUNK_POINTER) = ck;
}
/* 初始化一个内存池的头部 */
static void init_pool_head(pool *p,mem_size_t pool_size)
{
    p->start = (char*)p + sizeof(pool);
    p->fixed_size = pool_size;
    p->alloc_size = 0;
    p->prog_alloc_size = 0;
    p->alloc_list = NULL;
    p->free_list = (mp_chunk *)(p->start);  
    init_chunk_head(p->free_list,pool_size);        // 整个内存池是一整块大的free chunk
}

/* 找到ptr所属于的内存池 */
static pool* find_pool(memory_pool* mp, void* ptr) {
    pool* p = mp->list;
    while (p) {
        // 如果ptr地址大于内存池p的起始地址start，并且小于内存池最大可能地址
        // 那么可以知道：ptr属于内存池p
        if (p->start < (char*) ptr && p->start + p->fixed_size > (char*) ptr)
            break;
        p = p->next;
    }
    return p;
}

/* 添加一个内存池 */
static pool *add_pool(memory_pool *mp, mem_size_t new_pool_size)
{
    char *s = (char *)malloc(sizeof(pool) + new_pool_size * sizeof(char));
    if (!s)
        return NULL;
    pool *p = (pool *)s;
    init_pool_head(p,new_pool_size);
    p->pool_id = ++mp->id;
    p->next = mp->list;
    mp->list = p;
    return p;
}

/* 以内存块ck为中心，合并内存池p中的邻接空闲块 */
static void merge_free_chunks(memory_pool* mp, pool* p, mp_chunk* ck) 
{
    mp_chunk *ptr = ck;
    mp_chunk *head;
    while (ptr->is_free) 
    {
        head = ptr;
        // forward search reaches limit
        if ((char*) ptr - MP_CHUNK_POINTER < p->start) 
            break;
        // swtich to front block
        ptr = *(mp_chunk**)((char*)ptr - MP_CHUNK_POINTER);
    }
    ptr = (mp_chunk*)((char*)head + head->size);   // next neighbouring mp_chunk
    while ((char*) ptr < p->start + p->fixed_size && ptr->is_free) 
    {
        DELETE_FROM_LINKLIST(p->free_list, ptr);       // delete po from p->free_list
        head->size += ptr->size;                       // accumulate the size
        ptr = (mp_chunk*) ((char*) ptr + ptr->size);   // next neighbouring mp_chunk
    }
    *(mp_chunk**) ((char*) head + head->size - MP_CHUNK_POINTER) = head;

#ifdef MP_THREAD
    MP_UNLOCK(mp);
#endif
    return;
}

/* 内存池管理器初始化 */
memory_pool *memory_pool_init(mem_size_t pool_size, mem_size_t max_size)
{
    // 如果pool_size > max_size，不符合逻辑，直接返回
    if (pool_size > max_size)
    {
        return NULL;
    }
    // 分配一个内存池管理器，它不是一个内存池，而是一个综合管理器
    memory_pool *mp = (memory_pool *)malloc(sizeof(memory_pool));
    if (!mp)
        return NULL;
    mp->id = 0;
    mp->pool_size = pool_size;
    mp->max_size = max_size;
    mp->alloc_size = mp->pool_size;
    if (mp->alloc_size < mp->max_size)
        mp->auto_extend = 1;
#ifdef MP_THREAD
    pthread_mutex_init(&mp->lock, NULL);
#endif
    // 添加一个内存池
    if (!add_pool(mp,mp->pool_size))
    {
        // 失败，则释放mp
        free(mp);
        return NULL;
    }
    return mp;
}


void *memory_pool_alloc(memory_pool *mp, mem_size_t size)
{
    // 如果size<=0，无法申请，直接退出
    if (size <= 0)
        return NULL;
    // 算上块所需要的struct mp_chunk+一个指针大小struct *mp_chunk并且进行对齐
    // 得到total_size
    mem_size_t total_size = MP_ALIGN_SIZE(size + MP_CHUNK_HEAD + MP_CHUNK_POINTER, sizeof(long));
    // 大于pool_size无法分配，返回NULL
    if (total_size > mp->pool_size)
        return NULL;

#ifdef MP_THREAD
    MP_LOCK(mp);
#endif
FIND_FREE_CHUNK:
    pool *p = mp->list;
    while (p)
    {
        // 如果p指向的内存池空间不足，则去下一个内存池
        if (p->fixed_size - p->alloc_size < total_size)
        {
            p = p->next;
            continue;
        }
        // 获得内存池p的当前空闲块链表
        mp_chunk *free = p->free_list, *ck = NULL;
        while (free)
        {
            if (free->size >= total_size)
            {
                // 如果free块分割后剩余内存足够大足够装下信息块，则进行分割
                if (free->size - total_size > MP_CHUNK_HEAD + MP_CHUNK_POINTER)
                {
                    // 从free空闲块分头部割出一个非空闲块，
                    ck = free;
                    // 将free后移
                    free = (mp_chunk *)((char *)ck + total_size);
                    // 将原来的chunk头部信息复制到free空闲块
                    // 包括前驱后继信息以及is_free
                    *free = *ck;
                    // 分割后减小free空闲块大小
                    free->size -= total_size;
                    // 空闲块尾部指针指向空闲块自身
                    *(mp_chunk **)((char *)free + free->size -
                                   MP_CHUNK_POINTER) = free;
                    /* 更新内存池p的free_list */
                    if (!free->prev)
                    {
                        // 如果free空闲块没有前驱，即它是内存池的free_list第一个空闲块
                        p->free_list = free;
                    }
                    else
                    {
                        // 如果free空闲块有前驱，则重新链接前驱到free
                        free->prev->next = free;
                    }
                    if (free->next) // 如果free空闲块有后继，则重新链接后继
                        free->next->prev = free;

                    ck->is_free = 0;
                    ck->size = total_size;
                    // chunk尾部指针指向chunk本身，保存chunk首地址
                    *(mp_chunk **)((char *)ck + total_size -
                                   MP_CHUNK_POINTER) = ck;
                }
                else
                {
                    // 不够分割，而是进行整块分配
                    // ck->size==free->size
                    ck = free;
                    ck->is_free = 0;
                    // 将当前块free从free_list中删除
                    DELETE_FROM_LINKLIST(p->free_list, free);
                }
                // 将ck分配块插入到alloc_list
                INSERT_INTO_LINKLIST(p->alloc_list, ck);
                // 当前内存池已经分配给用户的内存大小增加
                p->alloc_size += ck->size;
                // 当前内存池已经分配给程序的内存大小增加
                p->prog_alloc_size += (ck->size - MP_CHUNK_HEAD - MP_CHUNK_POINTER);
#ifdef MP_THREAD
                MP_UNLOCK(mp);
#endif
                return (void *)((char *)ck + MP_CHUNK_HEAD);
            }
            // 不满足条件，去下一个空闲块
            free = free->next;
        }
        p = p->next;
    }
    // 当前可以扩展
    if (mp->auto_extend)
    {
        // 超过总内存限制
        if (mp->alloc_size + total_size > mp->max_size)
        {
            printf(" No enough memory! \n");
            goto err_out;
        }
        // 剩余可新增的大小，根据上面得知 increment >= total_size
        mem_size_t increment  = mp->max_size - mp->alloc_size;
        // 如果increment大于标准内存池的大小，则按pool_size新增,
        // 否则按increment申请一个内存池
        mem_size_t new_size = increment >= mp->pool_size ? mp->pool_size
                                                 : increment;
        pool *p = NULL;                                         
        if((p = add_pool(mp,new_size)))
        {
            goto FIND_FREE_CHUNK;
            // 更新分配的所有内存池的大小总和
            mp->alloc_size += new_size;
        }else{
            printf("Failed to add a new pool! \n");
        }
    }
err_out:
#ifdef MP_THREAD
    MP_UNLOCK(mp);
#endif
    return NULL;
}

/* 释放已经分配的内存块 */
void memory_pool_free(memory_pool *mp, void *ptr)
{
    if (ptr == NULL || mp == NULL)
        return;
#ifdef MP_THREAD
    MP_LOCK(mp);
#endif
    pool *p = find_pool(mp, ptr);   // 找到ptr所属于的内存池
    mp_chunk *ck = (mp_chunk *)((char *)ptr - MP_CHUNK_HEAD);   // 返回ptr所属的内存块的头部的首地址
    DELETE_FROM_LINKLIST(p->alloc_list, ck);    // 从alloc_list中删除ck
    INSERT_INTO_LINKLIST(p->free_list, ck);     // 将ck加入free_list中
    ck->is_free = 1;                            // 标记为空闲
    // 减少内存池p的已分配内存大小
    p->alloc_size -= ck->size;                   
    // 减小内存池p的已分配程序空间大小              
    p->prog_alloc_size -= (ck->size - MP_CHUNK_HEAD - MP_CHUNK_POINTER);    
    merge_free_chunk(mp, p, ck);
    return;
}

/* 置空每一个内存池 */
void *memory_pool_clear(memory_pool *mp)
{
    // 如果mp不存在，那么返回NULL
    if (!mp)
        return NULL;
#ifdef MP_THREAD
    MP_LOCK(mp);
#endif
    pool *p = mp->list;
    while (p)
    {
        init_pool_head(p, p->fixed_size);
        p = p->next;
    }
#ifdef MP_THREAD
    MP_UNLOCK(mp);
#endif
    return mp;
}

/* 释放内存池管理器中的每一个内存池，并且释放内存池管理器 */
void memory_pool_destroy(memory_pool *mp)
{
    if (mp == NULL)
        return;
#ifdef MP_THREAD
    MP_LOCK(mp);
#endif
    pool *p = mp->list, *q = NULL;
    while (p)
    {
        q = p;
        p = p->next;
        free(q);
    }
#ifdef MP_THREAD
    MP_UNLOCK(mp);
    pthread_mutex_destroy(&mp->lock);
#endif
    free(mp);
    return;
}
/* 统计内存池管理器中的内存池个数 */
void memory_pool_count(memory_pool *mp, mem_size_t *pool_list_length)
{
#ifdef MP_THREAD
    MP_LOCK(mp);
#endif
    mem_size_t len = 0;
    pool *p = mp->list;
    while (p)
    {
        len++;
        p = p->next;
    }
    *pool_list_length = len;
#ifdef MP_THREAD
    MP_UNLOCK(mp);
#endif
}
/* 统计内存池中空闲块和分配块个数 */
void memory_pool_info(memory_pool *mp, pool *p,mem_size_t *free_list_length,mem_size_t *alloc_list_length)
{
#ifdef MP_THREAD
    MP_LOCK(mp);
#endif
    mem_size_t len1 = 0, len2 = 0;
    mp_chunk *ck = p->free_list;
    while (ck)
    {
        len1++;
        ck = ck->next;
    }
    ck = p->alloc_list;
    while (ck)
    {
        len2++;
        ck = ck->next;
    }
    *free_list_length = len1;
    *alloc_list_length = len2;
#ifdef MP_THREAD
    MP_UNLOCK(mp);
#endif
}


