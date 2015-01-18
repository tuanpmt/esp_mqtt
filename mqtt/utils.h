#ifndef _UTILS_H_
#define	_UTILS_H_

#include "c_types.h"

uint32_t UTILS_Atoh(const int8_t *s);
uint8_t UTILS_StrToIP(const int8_t* str, void *ip);
uint8_t UTILS_IsIPV4 (int8_t *str);
char *ULTILS_StrReplace(char *orig, char *rep, char *with);
#endif
