/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Header file for MD5 hardware acceleration
 *
 * Copyright (c) 2012  Samsung Electronics
 */
#ifndef __HW_MD5_H
#define __HW_MD5_H
#include <hash.h>

/**
 * Computes hash value of input pbuf using h/w acceleration
 *
 * @param in_addr	A pointer to the input buffer
 * @param bufleni	Byte length of input buffer
 * @param out_addr	A pointer to the output buffer. When complete
 *			16 bytes are copied to pout[0]...pout[15]. Thus, a user
 *			should allocate at least 16 bytes at pOut in advance.
 * @param chunk_size	chunk_size for md5
 */
void hw_md5(const uchar *in_addr, uint buflen, uchar *out_addr,
	     uint chunk_size);
#endif
