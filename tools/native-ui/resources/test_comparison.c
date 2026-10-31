#define __libc_start_main resource_probe_unused_entry
#include "resources_probe.c"
#undef __libc_start_main
#include <assert.h>

int main(void)
{
    stock_area_t clip={9,7,228,86};
    Target *a=malloc(sizeof(*a)),*b=malloc(sizeof(*b));assert(a && b);
    target_init(a,STOCK_CF_XRGB8888,&clip);target_init(b,STOCK_CF_XRGB8888,&clip);
    size_t alpha=0,inside=GUARD+12*STRIDE+20*4;
    assert(!visible_differences(a,b,STOCK_CF_XRGB8888,&alpha));
    b->memory[inside+3]^=1;
    assert(!visible_differences(a,b,STOCK_CF_XRGB8888,&alpha) && alpha==0);
    assert(visible_differences(a,b,STOCK_CF_ARGB8888,&alpha)==1 && alpha==1);
    b->memory[inside]^=1;
    assert(visible_differences(a,b,STOCK_CF_XRGB8888,&alpha)==1);
    b->memory[inside]^=1;b->memory[inside+3]^=1;
    size_t padding=GUARD+12*STRIDE+WIDTH*4;
    b->memory[padding]^=1;
    assert(!visible_differences(a,b,STOCK_CF_ARGB8888,&alpha));
    assert(invalid_writes(b,&clip)==1);
    b->memory[padding]^=1;b->memory[0]^=1;b->memory[GUARD]^=1;
    assert(invalid_writes(b,&clip)==2);
    free(a);free(b);
    puts("PASS: comparator ignores only XRGB X, requires ARGB alpha and RGB, checks target padding/guards/clip");
    return 0;
}
