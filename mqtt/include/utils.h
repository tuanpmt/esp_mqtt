#ifndef _UTILS_H_
#define _UTILS_H_

#include "c_types.h"

uint32_t ICACHE_FLASH_ATTR UTILS_Atoh(const char *s);
uint8_t ICACHE_FLASH_ATTR UTILS_StrToIP(const char *str, void *ip);
uint8_t ICACHE_FLASH_ATTR UTILS_IsIPV4 (const char *str);
#endif
