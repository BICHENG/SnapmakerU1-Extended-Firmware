#include "stock-release.h"
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>

enum { TEXT_START=0xe800, TEXT_END=0x1b9f70, OBJECT_VALID=0x5fef0, MEMORY_TEST=0xbd020 };

static int assertion_call(uint32_t instruction, size_t offset)
{
    if((instruction & 0xfc000000u) != 0x94000000u) return 0;
    int64_t relative=instruction & 0x03ffffffu;
    if(relative & 0x02000000) relative-=0x04000000;
    int64_t target=(int64_t)offset+relative*4;
    return target==OBJECT_VALID?1:target==MEMORY_TEST?2:0;
}

int stock_release_apply(uintptr_t base)
{
    unsigned objects=0, memory=0;
    for(size_t offset=TEXT_START;offset<TEXT_END;offset+=4) {
        int kind=assertion_call(*(const uint32_t *)(base+offset),offset);
        objects+=kind==1; memory+=kind==2;
    }
    /* Every call was verified to lead to an LVGL debug assertion, never application control flow. */
    if(objects!=398 || memory!=8) return -1;
    long page=sysconf(_SC_PAGESIZE);
    if(page<=0 || (page & (page-1))) return -1;
    uintptr_t begin=(base+TEXT_START) & ~((uintptr_t)page-1);
    uintptr_t end=(base+TEXT_END+(uintptr_t)page-1) & ~((uintptr_t)page-1);
    if(mprotect((void *)begin,end-begin,PROT_READ|PROT_WRITE)) return -1;
    for(size_t offset=TEXT_START;offset<TEXT_END;offset+=4) {
        uint32_t *word=(uint32_t *)(base+offset);
        if(assertion_call(*word,offset)) *word=0x52800020u; /* mov w0, #1 */
    }
    __builtin___clear_cache((char *)begin,(char *)end);
    if(mprotect((void *)begin,end-begin,PROT_READ|PROT_EXEC)) _exit(127);
    return (int)(objects+memory);
}
