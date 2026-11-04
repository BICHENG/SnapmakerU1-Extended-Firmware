#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../apps/stock-display/stock-release.c"

static uint32_t call(size_t offset,size_t target) {
    return 0x94000000u | ((uint32_t)(((int64_t)target-(int64_t)offset)/4)&0x03ffffffu);
}
int main(int argc,char **argv) {
    size_t size=0x1ba000;
    uint8_t *image=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(image!=MAP_FAILED);
    if(argc==2) {
        FILE *file=fopen(argv[1],"rb");assert(file);
        assert(fread(image,1,size,file)==size);fclose(file);
    } else {
        for(unsigned i=0;i<406;i++) {
            size_t offset=TEXT_START+i*4;
            *(uint32_t *)(image+offset)=call(offset,i<398?OBJECT_VALID:MEMORY_TEST);
        }
    }
    uint8_t *before=malloc(size);assert(before);memcpy(before,image,size);
    /* An incomplete or previously changed image is rejected without partial writes. */
    size_t first=TEXT_START;
    while(!assertion_call(*(uint32_t *)(image+first),first))first+=4;
    *(uint32_t *)(image+first)=0xd503201fu;
    assert(stock_release_apply((uintptr_t)image)==-1);
    for(size_t i=0;i<size;i++)if(i<first || i>=first+4)assert(image[i]==before[i]);
    memcpy(image+first,before+first,4);
    assert(stock_release_apply((uintptr_t)image)==406);
    unsigned changed=0;
    for(size_t i=0;i<size;i+=4) {
        uint32_t old=*(uint32_t *)(before+i),now=*(uint32_t *)(image+i);
        if(old!=now) { assert(i>=TEXT_START && i<TEXT_END && assertion_call(old,i));assert(now==0x52800020u);changed++; }
    }
    assert(changed==406);
    assert(!memcmp(image+OBJECT_VALID,before+OBJECT_VALID,0x88));
    assert(!memcmp(image+MEMORY_TEST,before+MEMORY_TEST,0x60));
    assert(stock_release_apply((uintptr_t)image)==-1);
    free(before);assert(!munmap(image,size));
    puts("release assertions: exact calls only, validation APIs retained, mismatch leaves image unchanged");
}
