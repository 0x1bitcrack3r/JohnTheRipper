/*
 * OpenCL macros
 *
 * Copyright (c) 2014, magnum
 * This software is hereby released to the general public under
 * the following terms: Redistribution and use in source and binary
 * forms, with or without modification, are permitted.
 *
 * NOTICE: After changes in headers, you probably need to drop cached
 * kernels to ensure the changes take effect:
 *
 * For nvidia:
 * rm -fr ~/.nv/ComputeCache
 *
 * AMD, Apple, some others
 * rm ../run/kernels/?*.bin
 *
 */

#ifndef _OPENCL_MISC_H
#define _OPENCL_MISC_H

#include "opencl_device_info.h"

#if cpu(DEVICE_INFO) || gpu_amd(DEVICE_INFO) || nvidia_sm_5x(DEVICE_INFO)
#define USE_BITSELECT
#elif gpu_nvidia(DEVICE_INFO) && !nvidia_sm_5x(DEVICE_INFO)
#define OLD_NVIDIA
#endif

#define CONCAT(TYPE,WIDTH)	TYPE ## WIDTH
#define VECTOR(x, y)		CONCAT(x, y)

/* host code may pass -DV_WIDTH=2 or some other width */
#if defined(V_WIDTH) && V_WIDTH > 1
#define MAYBE_VECTOR_UINT	VECTOR(uint, V_WIDTH)
#define MAYBE_VECTOR_ULONG	VECTOR(ulong, V_WIDTH)
#else
#define MAYBE_VECTOR_UINT	uint
#define MAYBE_VECTOR_ULONG	ulong
#define SCALAR
#endif

/* Workaround for problem seen with 9600GT */
#ifdef OLD_NVIDIA
#define MAYBE_CONSTANT	__global const
#else
#define MAYBE_CONSTANT	__constant
#endif

/* Warning: This macro can't be used with eg. SWAP32(*p++) because it will
   be incremented twice */
#ifdef USE_BITSELECT
#define SWAP32(n)	bitselect(rotate(n, 24U), rotate(n, 8U), 0x00FF00FFU)
#else
inline uint SWAP32(uint x)
{
	x = rotate(x, 16U);
	return ((x & 0x00FF00FF) << 8) + ((x >> 8) & 0x00FF00FF);
}
#endif

#if defined(SCALAR)
#define VSWAP32 SWAP32
#else
/* Vector-capable swap32() */
inline MAYBE_VECTOR_UINT VSWAP32(MAYBE_VECTOR_UINT x)
{
	x = rotate(x, 16U);
	return ((x & 0x00FF00FF) << 8) + ((x >> 8) & 0x00FF00FF);
}
#endif

#define GETCHAR(buf, index) (((uchar*)(buf))[(index)])
#define GETCHAR_G(buf, index) (((__global uchar*)(buf))[(index)])
#define GETCHAR_BE(buf, index) (((uchar*)(buf))[(index) ^ 3])
#define LASTCHAR_BE(buf, index, val) (buf)[(index)>>2] = ((buf)[(index)>>2] & (0xffffff00U << ((((index) & 3) ^ 3) << 3))) + ((val) << ((((index) & 3) ^ 3) << 3))

#if gpu_amd(DEVICE_INFO) || no_byte_addressable(DEVICE_INFO) || !defined(SCALAR)
#define PUTCHAR(buf, index, val) (buf)[(index)>>2] = ((buf)[(index)>>2] & ~(0xffU << (((index) & 3) << 3))) + ((val) << (((index) & 3) << 3))
#define PUTCHAR_BE(buf, index, val) (buf)[(index)>>2] = ((buf)[(index)>>2] & ~(0xffU << ((((index) & 3) ^ 3) << 3))) + ((val) << ((((index) & 3) ^ 3) << 3))
#define PUTCHAR_G	PUTCHAR
#define PUTCHAR_BE_G	PUTCHAR_BE
#define PUTSHORT(buf, index, val) (buf)[(index)>>1] = ((buf)[(index)>>1] & ~(0xffffU << (((index) & 1) << 4))) + ((val) << (((index) & 1) << 4))
#define PUTSHORT_BE(buf, index, val) (buf)[(index)>>1] = ((buf)[(index)>>1] & ~(0xffffU << ((((index) & 1) ^ 3) << 4))) + ((val) << ((((index) & 1) ^ 3) << 4))
#define XORCHAR_BE(buf, index, val) (buf)[(index)>>2] = ((buf)[(index)>>2]) ^ ((val) << ((((index) & 3) ^ 3) << 3))
#else
#define PUTCHAR(buf, index, val) ((uchar*)(buf))[index] = (val)
#define PUTCHAR_G(buf, index, val) ((__global uchar*)(buf))[(index)] = (val)
#define PUTCHAR_BE(buf, index, val) ((uchar*)(buf))[(index) ^ 3] = (val)
#define PUTCHAR_BE_G(buf, index, val) ((__global uchar*)(buf))[(index) ^ 3] = (val)
#define PUTSHORT(buf, index, val) ((ushort*)(buf))[index] = (val)
#define PUTSHORT_BE(buf, index, val) ((ushort*)(buf))[(index) ^ 1] = (val)
#define XORCHAR_BE(buf, index, val) ((uchar*)(buf))[(index) ^ 3] ^= (val)
#endif

/* requires int/uint */
#define dump_stuff_msg(msg, x, size) do {	  \
		uint ii; \
		printf("%s : ", msg); \
		for (ii = 0; ii < size/4; ii++) \
			printf("%08x ", SWAP32(x[ii])); \
		printf("\n"); \
	} while (0)

/* requires int/uint */
#define dump_stuff_be_msg(msg, x, size) do {	  \
		uint ii; \
		printf("%s : ", msg); \
		for (ii = 0; ii < size/4; ii++) \
			printf("%08x ", x[ii]); \
		printf("\n"); \
	} while (0)

#endif
