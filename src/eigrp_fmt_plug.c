/*
 * Cracker for EIGRP (Cisco's proprietary routing protocol) MD5 authentication.
 * http://tools.ietf.org/html/draft-savage-eigrp-00
 *
 * This is dedicated to Darya. You inspire me.
 *
 * This software is Copyright (c) 2014, Dhiru Kholia <dhiru [at] openwall.com>,
 * and it is hereby released to the general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 */

#if FMT_EXTERNS_H
extern struct fmt_main fmt_eigrp;
#elif FMT_REGISTERS_H
john_register_one(&fmt_eigrp);
#else

#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#define OMP_SCALE 2048 // XXX
#endif

#include "arch.h"
#include "md5.h"
#include "misc.h"
#include "common.h"
#include "formats.h"
#include "params.h"
#include "options.h"
#include "memdbg.h"

#define FORMAT_LABEL            "eigrp"
#define FORMAT_NAME             "EIGRP MD5 authentication *BROKEN*"  // reduce expectations ;)
#define FORMAT_TAG              "$eigrp$"
#define TAG_LENGTH              (sizeof(FORMAT_TAG) - 1)
#define ALGORITHM_NAME          "MD5 32/" ARCH_BITS_STR
#define BENCHMARK_COMMENT       ""
#define BENCHMARK_LENGTH        0
#define PLAINTEXT_LENGTH        81 // IOU accepts larger strings but doesn't use them fully!
#define BINARY_SIZE             16 // MD5 hash or first 16 bytes of HMAC-SHA256
#define BINARY_ALIGN            sizeof(ARCH_WORD_32)
#define SALT_SIZE               sizeof(struct custom_salt)
#define SALT_ALIGN              MEM_ALIGN_NONE
#define MIN_KEYS_PER_CRYPT      1
#define MAX_KEYS_PER_CRYPT      1
#define HEXCHARS                "0123456789abcdef"

static struct fmt_tests tests[] = {
	{"$eigrp$2$020500000000000000000000000000000000002a000200280002001000000001000000000000000000000000$0$XXX$1a42aaf8ebe2f766100ea1fa05a5fa55", "password12345"},
	{"$eigrp$2$020500000000000000000000000000000000002a000200280002001000000001000000000000000000000000$0$XXX$f29e7d44351d37e6fc71e2aacca63d28", "1234567812345"},
	{"$eigrp$2$020500000000000000000000000000000000002a000200280002001000000001000000000000000000000000$1$0001000c010001000000000f000400080500030000f5000c0000000400$560c87396267310978883da92c0cff90", "password12345"},
	{NULL}
};

static char (*saved_key)[PLAINTEXT_LENGTH + 1];
static int *saved_len;
static ARCH_WORD_32 (*crypt_out)[BINARY_SIZE / sizeof(ARCH_WORD_32)];

static struct custom_salt {
	int length;
	int algo_type;
	int have_extra_salt;
	int extra_salt_length;
	unsigned char salt[1024];
	unsigned char extra_salt[1024];
} *cur_salt;

static void init(struct fmt_main *self)
{
#ifdef _OPENMP
	int omp_t = omp_get_num_threads();

	self->params.min_keys_per_crypt *= omp_t;
	omp_t *= OMP_SCALE;
	self->params.max_keys_per_crypt *= omp_t;
#endif
	saved_key = mem_calloc_tiny(sizeof(*saved_key) *
		self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	saved_len = mem_calloc_tiny(sizeof(*saved_len) *
		self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	crypt_out = mem_calloc_tiny(sizeof(*crypt_out) *
		self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
}

// XXX make me stronger!
static int valid(char *ciphertext, struct fmt_main *self)
{
	char *p, *q = NULL;
	int len;

	p = ciphertext;

	if (!strncmp(p, FORMAT_TAG, TAG_LENGTH))
		p += TAG_LENGTH;
	if (!p)
		return 0;
	if (*p != '2')  // MD5 hashes only, currently
		return 0;
	if (*(p+1) != '$')
		return 0;

	q = strrchr(ciphertext, '$');
	if (!q)
		return 0;
	q = q + 1;

	if (strlen(q) > BINARY_SIZE * 2)  // check hash size and data
		return 0;
	len = strspn(q, HEXCHARS);
	if (len != BINARY_SIZE * 2 || len != strlen(q))
		return 0;

	return 1;
}

static void *get_salt(char *ciphertext)
{
	static struct custom_salt cs;
	int i, len;
	char *p, *q;
	memset(&cs, 0, SALT_SIZE);

	if (!strncmp(ciphertext, FORMAT_TAG, TAG_LENGTH))
		ciphertext += TAG_LENGTH;

	p = ciphertext;
	cs.algo_type = atoi(p);
	p = p + 2; // salt start
	q = strchr(p, '$');
	len = (q - p) / 2;
	cs.length = len;

	for (i = 0; i < len; i++)
		cs.salt[i] = (atoi16[ARCH_INDEX(p[2 * i])] << 4) |
			atoi16[ARCH_INDEX(p[2 * i + 1])];

	q = q + 1;
	cs.have_extra_salt = atoi(q);

	if (cs.have_extra_salt == 1) {
		p = q + 2;
		q = strchr(p, '$');
		cs.extra_salt_length = (q - p) / 2;
		for (i = 0; i < cs.extra_salt_length; i++)
			cs.extra_salt[i] = (atoi16[ARCH_INDEX(p[2 * i])] << 4) |
				atoi16[ARCH_INDEX(p[2 * i + 1])];
	}

	return &cs;
}

static void *get_binary(char *ciphertext)
{
	static union {
		unsigned char c[BINARY_SIZE];
		ARCH_WORD dummy;
	} buf;
	unsigned char *out = buf.c;
	char *p;
	int i;
	p = strrchr(ciphertext, '$') + 1;
	for (i = 0; i < BINARY_SIZE; i++) {
		out[i] =
			(atoi16[ARCH_INDEX(*p)] << 4) |
			atoi16[ARCH_INDEX(p[1])];
		p += 2;
	}

	return out;
}

static int get_hash_0(int index) { return crypt_out[index][0] & 0xf; }
static int get_hash_1(int index) { return crypt_out[index][0] & 0xff; }
static int get_hash_2(int index) { return crypt_out[index][0] & 0xfff; }
static int get_hash_3(int index) { return crypt_out[index][0] & 0xffff; }
static int get_hash_4(int index) { return crypt_out[index][0] & 0xfffff; }
static int get_hash_5(int index) { return crypt_out[index][0] & 0xffffff; }
static int get_hash_6(int index) { return crypt_out[index][0] & 0x7ffffff; }

static void set_salt(void *salt)
{
	cur_salt = (struct custom_salt *)salt;
}

static int crypt_all(int *pcount, struct db_salt *salt)
{
	int count = *pcount;
	int index = 0;
#ifdef _OPENMP
#pragma omp parallel for
	for (index = 0; index < count; index++)
#endif
	{
		MD5_CTX ctx;
		MD5_Init(&ctx);
		MD5_Update(&ctx, cur_salt->salt, cur_salt->length);
		MD5_Update(&ctx, saved_key[index], saved_len[index]);
		MD5_Update(&ctx, "\x00\x00\x00", 3);  // WTF!
		// do we have extra_salt?
		if (cur_salt->have_extra_salt) {
			MD5_Update(&ctx, cur_salt->extra_salt, cur_salt->extra_salt_length);
		}
		MD5_Final((unsigned char*)crypt_out[index], &ctx);
	}
	return count;
}

static int cmp_all(void *binary, int count)
{
	int index = 0;
#ifdef _OPENMP
	for (; index < count; index++)
#endif
		if (((ARCH_WORD_32*)binary)[0] == crypt_out[index][0])
			return 1;
	return 0;
}

static int cmp_one(void *binary, int index)
{
	return !memcmp(binary, crypt_out[index], BINARY_SIZE);
}

static int cmp_exact(char *source, int index)
{
	return 1;
}

static void eigrp_set_key(char *key, int index)
{
	saved_len[index] = strlen(key);

	/* strncpy will pad with zeros, which is needed */
	strncpy(saved_key[index], key, sizeof(saved_key[0]));
}

static char *get_key(int index)
{
	return saved_key[index];
}

struct fmt_main fmt_eigrp = {
	{
		FORMAT_LABEL,
		FORMAT_NAME,
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		PLAINTEXT_LENGTH,
		BINARY_SIZE,
		DEFAULT_ALIGN,
		SALT_SIZE,
		DEFAULT_ALIGN,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
		FMT_CASE | FMT_8_BIT | FMT_OMP,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		tests
	}, {
		init,
		fmt_default_done,
		fmt_default_reset,
		fmt_default_prepare,
		valid,
		fmt_default_split,
		get_binary,
		get_salt,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		fmt_default_source,
		{
			fmt_default_binary_hash_0,
			fmt_default_binary_hash_1,
			fmt_default_binary_hash_2,
			fmt_default_binary_hash_3,
			fmt_default_binary_hash_4,
			fmt_default_binary_hash_5,
			fmt_default_binary_hash_6
		},
		fmt_default_salt_hash,
		set_salt,
		eigrp_set_key,
		get_key,
		fmt_default_clear_keys,
		crypt_all,
		{
			get_hash_0,
			get_hash_1,
			get_hash_2,
			get_hash_3,
			get_hash_4,
			get_hash_5,
			get_hash_6
		},
		cmp_all,
		cmp_one,
		cmp_exact
	}
};

#endif
