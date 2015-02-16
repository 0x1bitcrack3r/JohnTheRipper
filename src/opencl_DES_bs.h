/*
 * This software is Copyright (c) 2012 Sayantan Datta <std2048 at gmail dot com>
 * and it is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without modification, are permitted.
 * Based on Solar Designer implementation of DES_bs_b.c in jtr-v1.7.9
 */


#ifndef _JOHN_DES_BS_H
#define _JOHN_DES_BS_H

#include "arch.h"
#include "common-opencl.h"
#include "opencl_DES_hst_dev_shared.h"
#include "loader.h"

#define DES_BS_OPENCL_ALGORITHM_NAME		"DES OpenCL"

#define DES_BS_DEPTH			32
#define DES_BS_LOG2			5

#define WORD                      	int

#define DES_bs_vector			WORD

#define MULTIPLIER                      (WORK_GROUP_SIZE*256)
#define PLAINTEXT_LENGTH		8

#define MIN_KEYS_PER_CRYPT		(DES_BS_DEPTH*MULTIPLIER)
#define MAX_KEYS_PER_CRYPT		(DES_BS_DEPTH*MULTIPLIER)

#define GWS_CONFIG		        "des_GWS"

#define	MAX_DEVICES_PER_PLATFORM	10
#define DES_BS_EXPAND                   1

#define get_key_body() {						\
	static char out[PLAINTEXT_LENGTH + 1];				\
	unsigned int section, block;					\
	unsigned char *src;						\
	char *dst;							\
									\
	if (cmp_out == NULL || cmp_out[0] == 0 ||			\
	    index > 32 * cmp_out[0] || cmp_out[0] > num_loaded_hashes)	\
		section = index / DES_BS_DEPTH;				\
	else								\
		section = cmp_out[2 * (index/DES_BS_DEPTH) + 1];	\
									\
	if (section > num_set_keys / 32) {				\
		fprintf(stderr, "Get key error! %d %d\n", section,	\
			num_set_keys);					\
		section = 0;						\
		if (num_set_keys)					\
			error();					\
	}								\
	block  = index % DES_BS_DEPTH;					\
									\
	src = opencl_DES_bs_all[section].pxkeys[block];			\
	dst = out;							\
	while (dst < &out[PLAINTEXT_LENGTH] && (*dst = *src)) {		\
		src += sizeof(DES_bs_vector) * 8;			\
		dst++;							\
	}								\
	*dst = 0;							\
									\
	return out;							\
}

unsigned int CC_CACHE_ALIGN index768[0x300];

typedef struct {

	unsigned char *pxkeys[DES_BS_DEPTH]; /* Pointers into xkeys.c */
	unsigned int salt;	/* Salt value corresponding to E[] contents */
	DES_bs_vector Ens[48];	/* Pointers into B[] for non-salted E */
} opencl_DES_bs_combined;

struct fmt_main;

struct fmt_main fmt_opencl_DES;

extern opencl_DES_bs_combined *opencl_DES_bs_all;
extern opencl_DES_bs_transfer *opencl_DES_bs_data;
extern int opencl_DES_keys_changed;
extern DES_bs_vector *B;

extern void opencl_DES_bs_build_salt(WORD);
extern void opencl_DES_bs_init(int block);
extern void opencl_DES_bs_init_global_variables(void);

extern void opencl_DES_bs_set_salt(WORD salt);
extern void opencl_DES_bs_set_key(char *key, int index);
extern char *opencl_DES_bs_get_key(int index);
extern int opencl_DES_bs_crypt_25(int *pcount, struct db_salt *salt);
extern WORD *opencl_DES_bs_get_binary(char *ciphertext);

extern int opencl_DES_bs_get_hash_0(int index);
extern int opencl_DES_bs_get_hash_1(int index);
extern int opencl_DES_bs_get_hash_2(int index);
extern int opencl_DES_bs_get_hash_3(int index);
extern int opencl_DES_bs_get_hash_4(int index);
extern int opencl_DES_bs_get_hash_5(int index);
extern int opencl_DES_bs_get_hash_6(int index);

extern int opencl_DES_bs_cmp_one_b(WORD *binary, int count, int index);
extern WORD opencl_DES_raw_get_salt(char *ciphertext);
extern void opencl_DES_bs_select_device(struct fmt_main*);
extern void opencl_DES_clean_all_buffer(void);
extern void opencl_DES_reset(struct db_main *);
#endif
