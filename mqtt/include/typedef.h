/**
* \file
*   Standard Types definition
*/

#ifndef _TYPE_DEF_H_
#define _TYPE_DEF_H_

typedef char I8;
typedef unsigned char U8;
typedef short I16;
typedef unsigned short U16;
typedef long I32;
typedef unsigned long U32;
typedef unsigned long long U64;

#ifdef __cplusplus
// fix bug in c_types.h include, not defining BOOL, TRUE, FALSE for c++
#ifndef BOOL
#	define BOOL bool
#endif // BOOL
#ifndef TRUE
#	define TRUE true
#endif // TRUE
#ifndef FALSE
#	define FALSE false
#endif // FALSE
#endif // __cplusplus

#endif
