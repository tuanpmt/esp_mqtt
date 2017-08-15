#ifndef _SYS_TIME_
#define _SYS_TIME_

#include "c_types.h"

// returns time until boot in us
uint64_t ICACHE_FLASH_ATTR get_long_systime();

// returns lower half of time until boot in us
uint64_t ICACHE_FLASH_ATTR get_low_systime();

// initializes the timer
void init_long_systime();

#endif /* _SYS_TIME_ */
