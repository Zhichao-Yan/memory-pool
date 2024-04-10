#ifndef MP_H
#define MP_H

#ifdef MP_THREAD
#include <pthread.h>
#endif
#include <stdlib.h>
#include <string.h>

#define mem_size_t unsigned long long
#define KB (mem_size_t)(1 << 10)
#define MB (mem_size_t)(1 << 20)
#define GB (mem_size_t)(1 << 30)

typedef struct mp_chunk
{
    mem_size_t size;       // 块大小
    struct mp_chunk *prev; // 在当前块链表中的前驱
    struct mp_chunk *next; // 在当前块链表中的后继
    int is_free;           // 是否为占用
} mp_chunk;

typedef struct pool
{
    unsigned int pool_id;       // 当前池的编号
    char *start;                // 当前池的起始地址
    mem_size_t fixed_size;      // 当前内存池的固定大小
    mem_size_t alloc_size;      // 当前池内已分配的内存总大小
    mem_size_t prog_alloc_size; // 当前池内实际分配给应用程序的内存总大小(减去内存管理元信息)
    mp_chunk *free_list;        // 空闲块链表
    mp_chunk *alloc_list;       // 分配块链表（非空闲块链表）
    struct pool *next;          // 下一个池
}pool;

typedef struct memory_pool
{
    unsigned int id;       // 上次分配的内存池id
    int auto_extend;       // 标记该内存池能否自动延长
    mem_size_t pool_size;  // 固定值 每个内存池的标准大小
    mem_size_t max_size;   // 固定值 所有内存池大小的和的总上限
    mem_size_t alloc_size; // 统计值 当前已分配的内存池总大小
    struct pool *list;     // 内存池链表
#ifdef MP_THREAD
    pthread_mutex_t lock;
#endif
} memory_pool;

void *memory_pool_alloc(memory_pool *mp, mem_size_t size);
void memory_pool_free(memory_pool *mp, void *ptr);
void *memory_pool_clear(memory_pool *mp);
void memory_pool_destroy(memory_pool *mp);


void memory_pool_count(memory_pool *mp, mem_size_t *pool_list_length);
void memory_pool_info(memory_pool *mp, pool *p,
                      mem_size_t *free_list_length,
                      mem_size_t *alloc_list_length);

#endif