/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <xiaodong.zhao@artinchip.com>
 *  Desc: mpeg4 portab interface
 *
 */

#ifndef _PORTAB_H_
#define _PORTAB_H_

#ifdef WIN32

/* basic types
*/
#ifndef NULL
#ifdef  __cplusplus
#define NULL    0
#else
#define NULL    ((void *)0)
#endif
#endif

#define int8_t char
#define uint8_t unsigned char
#define int16_t short
#define uint16_t unsigned short
#define int32_t int
#define uint32_t unsigned int
#define int64_t __int64
#define uint64_t unsigned __int64

#define SU_MALLOC_ALIGN_SIZE	32

#elif defined (TARGET_PS2)

#define int8_t char
#define uint8_t unsigned char
#define int16_t short
#define uint16_t unsigned short
#define int32_t int
#define uint32_t unsigned int
#define	int64_t signed long
#define	uint64_t unsigned long

#define SU_MALLOC_ALIGN_SIZE	32

#elif defined (ARCH_BSP_V60)

#include <inttypes.h>
#define memcpy unaligned_memcpy
#define SU_MALLOC_ALIGN_SIZE	128

#elif defined (ARCH_TI_C6000)

#define SU_MALLOC_ALIGN_SIZE	64

#define int8_t char
#define __int8_t_defined
#define uint8_t unsigned char
#define int16_t short
#define uint16_t unsigned short
#define int32_t int
#define uint32_t unsigned int
#define	int64_t double
#define	uint64_t double

#elif defined (__CYGWIN__)
#include "sys/types.h"
#define SU_MALLOC_ALIGN_SIZE	128

#else // WIN32

#include <inttypes.h>
#define SU_MALLOC_ALIGN_SIZE	128

#endif

#endif // _PORTAB_H_
