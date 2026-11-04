#ifndef STOCK_RELEASE_H
#define STOCK_RELEASE_H
#include <stdint.h>
/* Call before vendor main, only after verifying the original 2.1.0.157 ELF hash. */
int stock_release_apply(uintptr_t base);
#endif
