#include <stdio.h>
#include "mp.h"

struct TAT {
    int T_T;
};

mem_size_t max_size = 2 * GB + 1000 * MB + 1000 * KB;
mem_size_t pool_size = 1 * GB + 500 * MB + 500 * KB;

int main() {
    memory_pool* mp = memory_pool_init(pool_size,max_size);
    struct TAT* tat = (struct TAT*)memory_pool_alloc(mp, sizeof(struct TAT));
    tat->T_T = 2333;
    printf("%d\n", tat->T_T);
    memory_pool_free(mp, tat);
    memory_pool_clear(mp);
    memory_pool_destroy(mp);
    return 0;
}