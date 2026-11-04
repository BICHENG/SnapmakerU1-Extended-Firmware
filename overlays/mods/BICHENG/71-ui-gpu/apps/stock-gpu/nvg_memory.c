#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *lv_malloc(size_t n) { return malloc(n); }
void *lv_malloc_zeroed(size_t n) { return calloc(1,n); }
void *lv_realloc(void *p,size_t n) { return realloc(p,n); }
void lv_free(void *p) { free(p); }
void *lv_memcpy(void *a,const void *b,size_t n) { return memcpy(a,b,n); }
void *lv_memset(void *a,int b,size_t n) { return memset(a,b,n); }
int lv_snprintf(char *s,size_t n,const char *fmt,...) {
    va_list args;va_start(args,fmt);int r=vsnprintf(s,n,fmt,args);va_end(args);return r;
}
int lv_sprintf(char *s,const char *fmt,...) {
    va_list args;va_start(args,fmt);int r=vsprintf(s,fmt,args);va_end(args);return r;
}
