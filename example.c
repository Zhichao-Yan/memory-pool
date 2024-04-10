#include <stdio.h>
#include "mp.h"

struct TAT {
    int T_T;
};

mem_size_t max_size = 2 * GB + 1000 * MB + 1000 * KB;
mem_size_t pool_size = 1 * GB + 500 * MB + 500 * KB;

int main() {
    memory_pool* mp = MemoryPoolInit(max_size, pool_size);
    struct TAT* tat = (struct TAT*) MemoryPoolAlloc(mp, sizeof(struct TAT));
    tat->T_T = 2333;
    printf("%d\n", tat->T_T);
    MemoryPoolFree(mp, tat);
    MemoryPoolClear(mp);
    MemoryPoolDestroy(mp);
    return 0;
}