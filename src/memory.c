/*
 * This file is part of John the Ripper password cracker,
 * Copyright (c) 1996-98,2010 by Solar Designer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "arch.h"
#include "misc.h"
#include "memory.h"
#include "common.h"

unsigned int mem_saving_level = 0;

// Add 'cleanup' methods for the mem_tiny_alloc.  VERY little cost, but
// allows us to check for mem leaks easier.
struct rm_list
{
	void *mem;
	struct rm_list *next;
};
static struct rm_list *mem_alloc_tiny_memory;

static void add_memory_link(void *v) {
	struct rm_list *p = mem_alloc(sizeof(struct rm_list));
	p->next = mem_alloc_tiny_memory;
	p->mem = v;
	mem_alloc_tiny_memory = p;
}
// call at program exit.
void cleanup_tiny_memory()
{
	struct rm_list *p = mem_alloc_tiny_memory, *p2;
	for (;;) {
		if (!p)
			return;
		free(p->mem);
		p2 = p->next;
		free(p);
		p = p2;
	}
}

void *mem_alloc(size_t size)
{
	void *res;

	if (!size) return NULL;

	if (!(res = malloc(size))) {
		fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
		error();
	}

	return res;
}

void *mem_alloc_tiny(size_t size, size_t align)
{
	static unsigned long buffer, bufree = 0;
	unsigned long start, end;

#if ARCH_ALLOWS_UNALIGNED
	if (mem_saving_level > 2) align = MEM_ALIGN_NONE;
#endif

	start = buffer + --align; start &= ~align;
	end = start + size;

	if (bufree >= end - buffer) {
		bufree -= end - buffer;
		buffer = end;
	} else
	if (size + align <= MEM_ALLOC_SIZE && bufree <= MEM_ALLOC_MAX_WASTE) {
		buffer = (unsigned long)mem_alloc(MEM_ALLOC_SIZE);
		add_memory_link((void*)buffer);
		bufree = MEM_ALLOC_SIZE;
		return mem_alloc_tiny(size, align + 1);
	} else {
		start = ((unsigned long) mem_alloc(size + align) + align);
		add_memory_link((void*)(start-align));
		start &= ~align;
	}

	return (void *)start;
}

void *mem_alloc_copy(size_t size, size_t align, void *src)
{
	return memcpy(mem_alloc_tiny(size, align), src, size);
}

char *str_alloc_copy(char *src)
{
	size_t size;

	if (!src) return "";
	if (!*src) return "";

	size = strlen(src) + 1;
	return (char *)memcpy(mem_alloc_tiny(size, MEM_ALIGN_NONE), src, size);
}

void dump_stuff(unsigned char * x, unsigned int size)
{
        unsigned int i;
        for(i=0;i<size;i++)
        {
                printf("%.2x", x[i]);
                if( (i%4)==3 )
                        printf(" ");
        }
        printf("\n");
}
void dump_stuff_msg(char *msg, unsigned char * x, unsigned int size) {
	printf("%s : ", msg);
	dump_stuff(x, size);
}

#if defined(MMX_COEF) || defined(NT_X86_64) || defined (MD5_SSE_PARA) || defined (MD4_SSE_PARA) || defined (SHA1_SSE_PARA)
#ifndef MMX_COEF
#define MMX_COEF	4
#endif

#define ROTATE_LEFT(i,c) (i) = (((i)<<(c))|((ARCH_WORD_32)(i)>>(32-(c))))

void alter_endianity(unsigned char * _x, unsigned int size)
{
	// since we are only using this in MMX code, we KNOW that we are using x86 CPU's which do not have problems
	// with non aligned 4 byte word access.  Thus, we use a faster swapping function.
	ARCH_WORD_32 tmp, *x = (ARCH_WORD_32*)_x;
	size>>=2;
	while (size--) {
		tmp = *x;
		ROTATE_LEFT(tmp, 16);
		*x++ = ((tmp & 0x00FF00FF) << 8) | ((tmp >> 8) & 0x00FF00FF);
	}
}

// These work for standard MMX_COEF buffers, AND for SSEi MMX_PARA multiple MMX_COEF blocks, where index will be mod(X * MMX_COEF) and not simply mod(MMX_COEF)
#define SHAGETPOS(i, index)		( (index&(MMX_COEF-1))*4 + ((i)&(0xffffffff-3) )*MMX_COEF + (3-((i)&3)) + (index>>(MMX_COEF>>1))*80*4*MMX_COEF ) //for endianity conversion
#define SHAGETOUTPOS(i, index)		( (index&(MMX_COEF-1))*4 + ((i)&(0xffffffff-3) )*MMX_COEF + (3-((i)&3)) + (index>>(MMX_COEF>>1))*20*MMX_COEF ) //for endianity conversion
#define GETPOS(i, index)		( (index&(MMX_COEF-1))*4 + ((i)&(0xffffffff-3) )*MMX_COEF +    ((i)&3)  + (index>>(MMX_COEF>>1))*64*MMX_COEF  )

void dump_stuff_mmx(unsigned char * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for(i=0;i<size;i++)
	{
		printf("%.2x", buf[GETPOS(i, index)]);
		if( (i%4)==3 )
			printf(" ");
	}
	printf("\n");
}
void dump_stuff_mmx_msg(char *msg, unsigned char * buf, unsigned int size, unsigned int index) {
	printf("%s : ", msg);
	dump_stuff_mmx(buf, size, index);
}

void dump_stuff_shammx(unsigned char * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for(i=0;i<size;i++)
	{
		printf("%.2x", buf[SHAGETPOS(i, index)]);
		if( (i%4)==3 )
			printf(" ");
	}
	printf("\n");
}
void dump_stuff_shammx_msg(char *msg, unsigned char * buf, unsigned int size, unsigned int index) {
	printf("%s : ", msg);
	dump_stuff_shammx(buf, size, index);
}
void dump_out_shammx(unsigned char * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for(i=0;i<size;i++)
	{
		printf("%.2x", buf[SHAGETOUTPOS(i, index)]);
		if( (i%4)==3 )
			printf(" ");
	}
	printf("\n");
}
void dump_out_shammx_msg(char *msg, unsigned char * buf, unsigned int size, unsigned int index) {
	printf("%s : ", msg);
	dump_out_shammx(buf, size, index);
}

#endif
