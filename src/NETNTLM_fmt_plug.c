/*
 * NETNTLM_fmt.c -- NTLM Challenge/Response
 *
 * Written by JoMo-Kun <jmk at foofus.net> in 2007
 * and placed in the public domain.
 *
 * Modified for performance, support for SSE2, ESS, OMP and UTF-8, by magnum
 * 2010-2011 and 2013.
 *
 * This algorithm is designed for performing brute-force cracking of the NTLM
 * (version 1) challenge/response pairs exchanged during network-based
 * authentication attempts [1]. The captured challenge/response pairs from these
 * attempts should be stored using the L0phtCrack 2.0 LC format, specifically:
 * username:unused:unused:lm response:ntlm response:challenge. For example:
 *
 * CORP\Administrator:::25B2B477CE101D83648BB087CE7A1C217F51C7FC64C0EBB1:
 * C8BD0C1630A9ECF7A95F494A8F0B2CB4A3F25B1225514304:1122334455667788
 *
 * It should be noted that a NTLM authentication response is not same as a NTLM
 * password hash, which can be extracted using tools such as FgDump [2]. NTLM
 * responses can be gathered via normal network capture or via tools which
 * perform layer 2 attacks, such as Ettercap [3] and Cain [4]. The responses can
 * also be harvested using a modified Samba service [5] in conjunction with
 * some trickery to convince the user to connect to it. I leave what that
 * trickery may actually be as an exercise for the reader (HINT: Karma, NMB
 * broadcasts, IE, Outlook, social engineering, ...).
 *
 * [1] http://davenport.sourceforge.net/ntlm.html#theNtlmResponse
 * [2] http://www.foofus.net/~fizzgig/fgdump/
 * [3] http://ettercap.sourceforge.net/
 * [4] http://www.oxid.it/cain.html
 * [5] http://www.foofus.net/jmk/smbchallenge.html
 *
 * This version supports Extended Session Security. This is what
 * is used when the "LM" hash ends in 32 zeros:
 *
 * DOMAIN\User:::c70e4fb229437ef300000000000000000000000000000000:
 * abf7762caf2b1bbfc5cfc1f46665249f049e0af72ae5b5a9:24ca92fdab441aa4
 *
 */

#include <string.h>
#include <openssl/des.h>

#include "arch.h"
#ifdef MD4_SSE_PARA
#define NBKEYS			(MMX_COEF * MD4_SSE_PARA)
#elif MMX_COEF
#define NBKEYS			MMX_COEF
#else
#ifdef _OPENMP
#define OMP_SCALE		2
#include <omp.h>
#endif
#endif
#include "sse-intrinsics.h"

#include "misc.h"
#include "common.h"
#include "formats.h"
#include "options.h"
#include "md4.h"
#include "md5.h"
#include "unicode.h"

#ifndef uchar
#define uchar unsigned char
#endif
#define MIN(a, b)		(((a) > (b)) ? (b) : (a))

#define FORMAT_LABEL		"netntlm"
#define FORMAT_NAME		"NTLMv1 C/R MD4 DES (ESS MD5)"
#define ALGORITHM_NAME		MD4_ALGORITHM_NAME
#define BENCHMARK_COMMENT	""
#define BENCHMARK_LENGTH	0
#define BINARY_SIZE		24
#define BINARY_ALIGN            ARCH_SIZE
#define PARTIAL_BINARY_SIZE	8
#define BARE_SALT_SIZE		8
#define SALT_SIZE		(0x80000 + BARE_SALT_SIZE)
#define SALT_ALIGN              ARCH_SIZE
#define CIPHERTEXT_LENGTH	48
#define TOTAL_LENGTH		(10 + 2 * 2 * BARE_SALT_SIZE + CIPHERTEXT_LENGTH)

#ifdef MMX_COEF
#define PLAINTEXT_LENGTH	27
#define MIN_KEYS_PER_CRYPT	NBKEYS
#define MAX_KEYS_PER_CRYPT	NBKEYS
#define GETPOS(i, index)	( (index&(MMX_COEF-1))*4 + ((i)&(0xffffffff-3))*MMX_COEF + ((i)&3) + (index>>(MMX_COEF>>1))*16*MMX_COEF*4 )
#define GETOUTPOS(i, index)	( (index&(MMX_COEF-1))*4 + ((i)&(0xffffffff-3))*MMX_COEF + ((i)&3) + (index>>(MMX_COEF>>1))*4*MMX_COEF*4 )
#else
#define PLAINTEXT_LENGTH	64
#define MIN_KEYS_PER_CRYPT	1
#define MAX_KEYS_PER_CRYPT	2048
#endif

static struct fmt_tests tests[] = {
	{"$NETNTLM$1122334455667788$BFCCAF26128EC95F9999C9792F49434267A1D9B0EF89BFFB", "g3rg3g3rg3g3rg3"},
#ifndef MMX_COEF /* exceeds max length for SSE */
	{"$NETNTLM$1122334455667788$E463FAA5D868ECE20CAE622474A2F440A652D642156AF863", "M1xedC4se%^&*@)##(blahblah!@#"},
#endif
	{"$NETNTLM$c75c20bff9baa71f4765f360625700b0$81f5ecd8a77fe819f7f6689a08a27ac705fc2e1bb00cecb2", "password"},
	{"$NETNTLM$1122334455667788$35B62750E1B9B3205C50D6BA351092C12A1B9B3CDC65D44A", "FooBarGerg"},
	{"$NETNTLM$1122334455667788$A4765EBFE83D345A7CB1660B8899251905164029F8086DDE", "visit www.foofus.net"},
	{"$NETNTLM$24ca92fdab441aa4c70e4fb229437ef3$abf7762caf2b1bbfc5cfc1f46665249f049e0af72ae5b5a9", "longpassword"},
	{"$NETNTLM$1122334455667788$B2B2220790F40C88BCFF347C652F67A7C4A70D3BEBD70233", "cory21"},
	{"", "g3rg3g3rg3g3rg3",               {"User", "", "", "lm-hash", "BFCCAF26128EC95F9999C9792F49434267A1D9B0EF89BFFB", "1122334455667788"} },
	{"", "FooBarGerg",                    {"User", "", "", "lm-hash", "35B62750E1B9B3205C50D6BA351092C12A1B9B3CDC65D44A", "1122334455667788"} },
	{"", "visit www.foofus.net",          {"User", "", "", "lm-hash", "A4765EBFE83D345A7CB1660B8899251905164029F8086DDE", "1122334455667788"} },
	{"", "password",                      {"ESS", "", "", "4765f360625700b000000000000000000000000000000000", "81f5ecd8a77fe819f7f6689a08a27ac705fc2e1bb00cecb2", "c75c20bff9baa71f"} },
	{"", "cory21",                        {"User", "", "", "lm-hash", "B2B2220790F40C88BCFF347C652F67A7C4A70D3BEBD70233", "1122334455667788"} },
	{NULL}
};

#ifdef MMX_COEF
static unsigned char (*saved_key);
static unsigned char (*nthash);
#ifndef MD4_SSE_PARA
static unsigned int total_len;
#endif
#else
static UTF16 (*saved_key)[PLAINTEXT_LENGTH + 1];
static int (*saved_key_length);
#endif

static uchar (*output)[PARTIAL_BINARY_SIZE];
static uchar (*crypt_key)[21]; // NT hash
static uchar *challenge;
static int keys_prepared;

static void set_key_utf8(char *_key, int index);
static void set_key_CP(char *_key, int index);

static void init(struct fmt_main *self)
{
#if defined (_OPENMP) && !defined(MMX_COEF)
	int omp_t = omp_get_max_threads();
	self->params.min_keys_per_crypt *= omp_t;
	omp_t *= OMP_SCALE;
	self->params.max_keys_per_crypt *= omp_t;
#endif

	if (options.utf8) {
		self->methods.set_key = set_key_utf8;
		self->params.plaintext_length = MIN(125, 3 * PLAINTEXT_LENGTH);
	} else {
		if (!options.ascii && !options.iso8859_1)
			self->methods.set_key = set_key_CP;
	}
#if MMX_COEF
	saved_key = mem_calloc_tiny(sizeof(*saved_key) * 64 * self->params.max_keys_per_crypt, MEM_ALIGN_SIMD);
	nthash = mem_calloc_tiny(sizeof(*nthash) * 16 * self->params.max_keys_per_crypt, MEM_ALIGN_SIMD);
#else
	saved_key = mem_calloc_tiny(sizeof(*saved_key) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	saved_key_length = mem_calloc_tiny(sizeof(*saved_key_length) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
#endif
	output = mem_calloc_tiny(sizeof(*output) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	crypt_key = mem_calloc_tiny(sizeof(*crypt_key) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
}

static int valid(char *ciphertext, struct fmt_main *self)
{
	char *pos;

	if (strncmp(ciphertext, "$NETNTLM$", 9)!=0) return 0;

	if ((strlen(ciphertext) != 74) && (strlen(ciphertext) != 90)) return 0;

	if ((ciphertext[25] != '$') && (ciphertext[41] != '$')) return 0;

	for (pos = &ciphertext[9]; atoi16[ARCH_INDEX(*pos)] != 0x7F; pos++);
	if (*pos != '$') return 0;

	for (pos++;atoi16[ARCH_INDEX(*pos)] != 0x7F; pos++);
	if (!*pos && ((pos - ciphertext - 26 == CIPHERTEXT_LENGTH) ||
	              (pos - ciphertext - 42 == CIPHERTEXT_LENGTH)))
		return 1;
	else
		return 0;
}

static char *prepare(char *split_fields[10], struct fmt_main *self)
{
	char *cp;
	char clientChal[17];

	if (!strncmp(split_fields[1], "$NETNTLM$", 9))
		return split_fields[1];
	if (!split_fields[3]||!split_fields[4]||!split_fields[5])
		return split_fields[1];

	if (strlen(split_fields[4]) != CIPHERTEXT_LENGTH)
		return split_fields[1];

	// this string suggests we have an improperly formatted NTLMv2
	if (!strncmp(&split_fields[4][32], "0101000000000000", 16))
		return split_fields[1];

	// Handle ESS (8 byte client challenge in "LM" field padded with zeros)
	if (strlen(split_fields[3]) == 48 && !strncmp(&split_fields[3][16],
	                                              "00000000000000000000000000000000", 32)) {
		memcpy(clientChal, split_fields[3],16);
		clientChal[16] = 0;
	}
	else
		clientChal[0] = 0;
	cp = mem_alloc(9+strlen(split_fields[5])+strlen(clientChal)+1+strlen(split_fields[4])+1);
	sprintf(cp, "$NETNTLM$%s%s$%s", split_fields[5], clientChal, split_fields[4]);

	if (valid(cp,self)) {
		char *cp2 = str_alloc_copy(cp);
		MEM_FREE(cp);
		return cp2;
	}
	MEM_FREE(cp);
	return split_fields[1];
}

static char *split(char *ciphertext, int index, struct fmt_main *self)
{
	static char out[TOTAL_LENGTH + 1];

	memset(out, 0, TOTAL_LENGTH + 1);
	memcpy(&out, ciphertext, TOTAL_LENGTH);
	strlwr(&out[8]); /* Exclude: $NETNTLM$ */

	return out;
}

static void *get_binary(char *ciphertext)
{
	static uchar *binary;
	int i;

	if (!binary) binary = mem_alloc_tiny(BINARY_SIZE, MEM_ALIGN_WORD);

	ciphertext = strrchr(ciphertext, '$') + 1;
	for (i=0; i<BINARY_SIZE; i++) {
		int j = i < 16 ? i + 8 : i - 16;
		binary[j] = (atoi16[ARCH_INDEX(ciphertext[i*2])])<<4;
		binary[j] |= (atoi16[ARCH_INDEX(ciphertext[i*2+1])]);
	}

	return binary;
}

static inline void setup_des_key(uchar key_56[], DES_key_schedule *ks)
{
	DES_cblock key;

	key[0] = key_56[0];
	key[1] = (key_56[0] << 7) | (key_56[1] >> 1);
	key[2] = (key_56[1] << 6) | (key_56[2] >> 2);
	key[3] = (key_56[2] << 5) | (key_56[3] >> 3);
	key[4] = (key_56[3] << 4) | (key_56[4] >> 4);
	key[5] = (key_56[4] << 3) | (key_56[5] >> 5);
	key[6] = (key_56[5] << 2) | (key_56[6] >> 6);
	key[7] = (key_56[6] << 1);

	DES_set_key(&key, ks);
}

static int crypt_all(int *pcount, struct db_salt *salt)
{
	int count = *pcount;
	DES_cblock (*lut)[0x100][0x100] = (void *)(challenge + BARE_SALT_SIZE);
	int i = 0;

	if (!keys_prepared) {
#if defined(MD4_SSE_PARA)
		SSEmd4body(saved_key, (unsigned int*)nthash, 1);
#elif defined(MMX_COEF)
		mdfourmmx(nthash, saved_key, total_len);
#else
#if defined(_OPENMP) || (MAX_KEYS_PER_CRYPT > 1)
#ifdef _OPENMP
#pragma omp parallel for
#endif
		for (i = 0; i < count; i++)
#endif
		{
			MD4_CTX ctx;

			MD4_Init( &ctx );
			MD4_Update(&ctx, saved_key[i], saved_key_length[i]);
			MD4_Final((uchar*)crypt_key[i], &ctx);
		}
#endif
		keys_prepared = 1;
	}

#ifdef MMX_COEF
	for(i = 0; i < NBKEYS; i++) {
		memcpy(&output[i],
		       (*lut)[nthash[GETOUTPOS(14, i)]][nthash[GETOUTPOS(15, i)]], 8);
	}
#else
#if defined(_OPENMP) || (MAX_KEYS_PER_CRYPT > 1)
#ifdef _OPENMP
#pragma omp parallel for default(none) private(i) shared(count, output, crypt_key, lut)
#endif
	for(i = 0; i < count; i++)
#endif
	{
		memcpy(&output[i],
		       (*lut)[crypt_key[i][14]][crypt_key[i][15]], 8);
	}
#endif

	return count;
}

static int cmp_all(void *binary, int count)
{
	int index;

#ifdef MMX_COEF // allow compiler to optimize
	for(index=0; index < NBKEYS; index++)
#else
	for(index=0; index < count; index++)
#endif
		if (((ARCH_WORD*)binary)[0] == ((ARCH_WORD*)output[index])[0])
			return 1;
	return 0;
}

static int cmp_one(void *binary, int index)
{
	return ((ARCH_WORD*)binary)[0] == ((ARCH_WORD*)output[index])[0];
}

static int cmp_exact(char *source, int index)
{
	DES_key_schedule ks;
	uchar binary[24];
#ifdef MMX_COEF
	int i;

	for (i = 0; i < 4; i++)
		((ARCH_WORD_32*)crypt_key[index])[i] = *(ARCH_WORD_32*)&nthash[GETOUTPOS(4 * i, index)];
#endif
	/* Hash is NULL padded to 21-bytes (postponed until now) */
	memset(&crypt_key[index][16], 0, 5);

	/* Split into three 7-byte segments for use as DES keys
	   Use each key to DES encrypt challenge
	   Concatenate output to for 24-byte NTLM response */
	setup_des_key(crypt_key[index], &ks);
	DES_ecb_encrypt((DES_cblock*)challenge, (DES_cblock*)&binary[8], &ks, DES_ENCRYPT);
	setup_des_key(&crypt_key[index][7], &ks);
	DES_ecb_encrypt((DES_cblock*)challenge, (DES_cblock*)&binary[16], &ks, DES_ENCRYPT);
	setup_des_key(&crypt_key[index][14], &ks);
	DES_ecb_encrypt((DES_cblock*)challenge, (DES_cblock*)binary, &ks, DES_ENCRYPT);

	return !memcmp(binary, get_binary(source), BINARY_SIZE);
}

static void *get_salt(char *ciphertext)
{
	static uchar *binary_salt;
	int i;

	if (!binary_salt) binary_salt = mem_alloc(SALT_SIZE);

	if (ciphertext[25] == '$') {
		// Server challenge
		ciphertext += 9;
		for (i = 0; i < BARE_SALT_SIZE; ++i)
			binary_salt[i] = (atoi16[ARCH_INDEX(ciphertext[i*2])] << 4) + atoi16[ARCH_INDEX(ciphertext[i*2+1])];
	} else {
		uchar es_salt[2*BARE_SALT_SIZE], k1[2*BARE_SALT_SIZE];
		MD5_CTX ctx;

		ciphertext += 9;
		// Extended Session Security,
		// Concatenate Server & Client challenges
		for (i = 0;i < 2 * BARE_SALT_SIZE; ++i)
			es_salt[i] = (atoi16[ARCH_INDEX(ciphertext[i*2])] << 4) + atoi16[ARCH_INDEX(ciphertext[i*2+1])];

		// MD5 the concatenated challenges, result is our key
		MD5_Init(&ctx);
		MD5_Update(&ctx, es_salt, 16);
		MD5_Final((void*)k1, &ctx);
		memcpy(binary_salt, k1, BARE_SALT_SIZE); // but only 8 bytes of it
	}

	{
		uchar key[7] = {0, 0, 0, 0, 0, 0, 0};
		DES_key_schedule ks;
		DES_cblock (*lut)[0x100][0x100] = (void *)(binary_salt + BARE_SALT_SIZE);
		int i, j;

		for (i = 0; i < 0x100; i++)
			for (j = 0; j < 0x100; j++) {
				key[0] = i; key[1] = j;
				setup_des_key(key, &ks);
				DES_ecb_encrypt((DES_cblock *)binary_salt, &(*lut)[i][j], &ks, DES_ENCRYPT);
			}
	}

	return (void*)binary_salt;
}

static void set_salt(void *salt)
{
	challenge = salt;
}

// ISO-8859-1 to UCS-2, directly into vector key buffer
static void netntlm_set_key(char *_key, int index)
{
#ifdef MMX_COEF
	const uchar *key = (uchar*)_key;
	unsigned int *keybuf_word = (unsigned int*)&saved_key[GETPOS(0, index)];
	unsigned int len, temp2;

#ifndef MD4_SSE_PARA
	if (!index)
		total_len = 0;
#endif
	len = 0;
	while((temp2 = *key++)) {
		unsigned int temp;
		if ((temp = *key++) && len < PLAINTEXT_LENGTH - 1)
		{
			temp2 |= (temp << 16);
			*keybuf_word = temp2;
		}
		else
		{
			temp2 |= (0x80 << 16);
			*keybuf_word = temp2;
			len++;
			goto key_cleaning;
		}
		len += 2;
		keybuf_word += MMX_COEF;
	}
	*keybuf_word = 0x80;

key_cleaning:
	keybuf_word += MMX_COEF;
	while(*keybuf_word) {
		*keybuf_word = 0;
		keybuf_word += MMX_COEF;
	}

#ifdef MD4_SSE_PARA
	((unsigned int *)saved_key)[14*MMX_COEF + (index&3) + (index>>2)*16*MMX_COEF] = len << 4;
#else
	total_len += len << (1 + ( (32/MMX_COEF) * index ) );
#endif
#else
#if ARCH_LITTLE_ENDIAN
	UTF8 *s = (UTF8*)_key;
	UTF16 *d = saved_key[index];
	while (*s)
		*d++ = *s++;
	*d = 0;
	saved_key_length[index] = (int)((char*)d - (char*)saved_key[index]);
#else
	UTF8 *s = (UTF8*)_key;
	UTF8 *d = (UTF8*)saved_key[index];
	while (*s) {
		*d++ = *s++;
		++d;
	}
	*d = 0;
	saved_key_length[index] = (int)((char*)d - (char*)saved_key[index]);
#endif
#endif
	keys_prepared = 0;
}

// Legacy codepage to UCS-2, directly into vector key buffer
static void set_key_CP(char *_key, int index)
{
#ifdef MMX_COEF
	const uchar *key = (uchar*)_key;
	unsigned int *keybuf_word = (unsigned int*)&saved_key[GETPOS(0, index)];
	unsigned int len;

#ifndef MD4_SSE_PARA
	if (!index)
		total_len = 0;
#endif
	len = 0;
	while((*keybuf_word = CP_to_Unicode[*key++])) {
		unsigned int temp;
		if ((temp = CP_to_Unicode[*key++]) && len < PLAINTEXT_LENGTH - 1)
			*keybuf_word |= (temp << 16);
		else {
			*keybuf_word |= (0x80 << 16);
			len++;
			goto key_cleaning_enc;
		}
		len += 2;
		keybuf_word += MMX_COEF;
	}
	*keybuf_word = 0x80;

key_cleaning_enc:
	keybuf_word += MMX_COEF;
	while(*keybuf_word) {
		*keybuf_word = 0;
		keybuf_word += MMX_COEF;
	}

#ifdef MD4_SSE_PARA
	((unsigned int *)saved_key)[14*MMX_COEF + (index&3) + (index>>2)*16*MMX_COEF] = len << 4;
#else
	total_len += len << (1 + ( (32/MMX_COEF) * index ) );
#endif
#else
	saved_key_length[index] = enc_to_utf16(saved_key[index],
	                                       PLAINTEXT_LENGTH + 1,
	                                       (uchar*)_key,
	                                       strlen(_key)) << 1;
	if (saved_key_length[index] < 0)
		saved_key_length[index] = strlen16(saved_key[index]);
#endif
	keys_prepared = 0;
}

// UTF-8 to UCS-2, directly into vector key buffer
static void set_key_utf8(char *_key, int index)
{
#ifdef MMX_COEF
	const UTF8 *source = (UTF8*)_key;
	unsigned int *keybuf_word = (unsigned int*)&saved_key[GETPOS(0, index)];
	UTF32 chl, chh = 0x80;
	unsigned int len = 0;

#ifndef MD4_SSE_PARA
	if (!index)
		total_len = 0;
#endif
	while (*source) {
		chl = *source;
		if (chl >= 0xC0) {
			unsigned int extraBytesToRead = opt_trailingBytesUTF8[chl & 0x3f];
			switch (extraBytesToRead) {
			case 2:
				++source;
				if (*source) {
					chl <<= 6;
					chl += *source;
				} else
					return;
			case 1:
				++source;
				if (*source) {
					chl <<= 6;
					chl += *source;
				} else
					return;
			case 0:
				break;
			default:
				return;
			}
			chl -= offsetsFromUTF8[extraBytesToRead];
		}
		source++;
		len++;
		if (*source && len < PLAINTEXT_LENGTH) {
			chh = *source;
			if (chh >= 0xC0) {
				unsigned int extraBytesToRead =
					opt_trailingBytesUTF8[chh & 0x3f];
				switch (extraBytesToRead) {
				case 2:
					++source;
					if (*source) {
						chh <<= 6;
						chh += *source;
					} else
						return;
				case 1:
					++source;
					if (*source) {
						chh <<= 6;
						chh += *source;
					} else
						return;
				case 0:
					break;
				default:
					return;
				}
				chh -= offsetsFromUTF8[extraBytesToRead];
			}
			source++;
			len++;
		} else {
			chh = 0x80;
			*keybuf_word = (chh << 16) | chl;
			keybuf_word += MMX_COEF;
			break;
		}
		*keybuf_word = (chh << 16) | chl;
		keybuf_word += MMX_COEF;
	}
	if (chh != 0x80 || len == 0) {
		*keybuf_word = 0x80;
		keybuf_word += MMX_COEF;
	}

	while(*keybuf_word) {
		*keybuf_word = 0;
		keybuf_word += MMX_COEF;
	}

#ifdef MD4_SSE_PARA
	((unsigned int *)saved_key)[14*MMX_COEF + (index&3) + (index>>2)*16*MMX_COEF] = len << 4;
#else
	total_len += len << (1 + ( (32/MMX_COEF) * index ) );
#endif
#else
	saved_key_length[index] = utf8_to_utf16(saved_key[index],
	                                        PLAINTEXT_LENGTH + 1,
	                                        (uchar*)_key,
	                                        strlen(_key)) << 1;
	if (saved_key_length[index] < 0)
		saved_key_length[index] = strlen16(saved_key[index]);
#endif
	keys_prepared = 0;
}

// Get the key back from the key buffer, from UCS-2
static char *get_key(int index)
{
#ifdef MMX_COEF
	unsigned int *keybuf_word = (unsigned int*)&saved_key[GETPOS(0, index)];
	static UTF16 key[PLAINTEXT_LENGTH + 1];
	unsigned int md4_size=0;
	unsigned int i=0;

	for(; md4_size < PLAINTEXT_LENGTH; i += MMX_COEF, md4_size++)
	{
		key[md4_size] = keybuf_word[i];
		key[md4_size+1] = keybuf_word[i] >> 16;
		if (key[md4_size] == 0x80 && key[md4_size+1] == 0) {
			key[md4_size] = 0;
			break;
		}
		++md4_size;
		if (key[md4_size] == 0x80 && ((keybuf_word[i+MMX_COEF]&0xFFFF) == 0 || md4_size == PLAINTEXT_LENGTH)) {
			key[md4_size] = 0;
			break;
		}
	}
	return (char*)utf16_to_enc(key);
#else
#if ARCH_LITTLE_ENDIAN
	return (char*)utf16_to_enc(saved_key[index]);
#else
	int i;
	UTF16 Tmp[80];
	UTF8 *p = (UTF8*)saved_key[index], *p2 = (UTF8*)Tmp;
	for (i = 0; i < saved_key_length[index]; i += 2) {
		p2[i] = p[i+1];
		p2[i+1] = p[i];
	}
	p2[i] = 0;
	p2[i+1] = 0;
	return (char*)utf16_to_enc(Tmp);
#endif
#endif
}

static int salt_hash(void *salt)
{
	return *(ARCH_WORD_32 *)salt & (SALT_HASH_SIZE - 1);
}

static int binary_hash_0(void *binary) { return *(ARCH_WORD_32 *)binary & 0xF; }
static int binary_hash_1(void *binary) { return *(ARCH_WORD_32 *)binary & 0xFF; }
static int binary_hash_2(void *binary) { return *(ARCH_WORD_32 *)binary & 0xFFF; }
static int binary_hash_3(void *binary) { return *(ARCH_WORD_32 *)binary & 0xFFFF; }
static int binary_hash_4(void *binary) { return *(ARCH_WORD_32 *)binary & 0xFFFFF; }
static int binary_hash_5(void *binary) { return *(ARCH_WORD_32 *)binary & 0xFFFFFF; }
static int binary_hash_6(void *binary) { return *(ARCH_WORD_32 *)binary & 0x7FFFFFF; }

static int get_hash_0(int index) { return *(ARCH_WORD_32 *)output[index] & 0xF; }
static int get_hash_1(int index) { return *(ARCH_WORD_32 *)output[index] & 0xFF; }
static int get_hash_2(int index) { return *(ARCH_WORD_32 *)output[index] & 0xFFF; }
static int get_hash_3(int index) { return *(ARCH_WORD_32 *)output[index] & 0xFFFF; }
static int get_hash_4(int index) { return *(ARCH_WORD_32 *)output[index] & 0xFFFFF; }
static int get_hash_5(int index) { return *(ARCH_WORD_32 *)output[index] & 0xFFFFFF; }
static int get_hash_6(int index) { return *(ARCH_WORD_32 *)output[index] & 0x7FFFFFF; }

struct fmt_main fmt_NETNTLM = {
	{
		FORMAT_LABEL,
		FORMAT_NAME,
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		PLAINTEXT_LENGTH,
		BINARY_SIZE,
		BINARY_ALIGN,
		SALT_SIZE,
		SALT_ALIGN,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
#ifndef MMX_COEF
		FMT_OMP |
#endif
		FMT_CASE | FMT_8_BIT | FMT_SPLIT_UNIFIES_CASE | FMT_UNICODE | FMT_UTF8,
		tests
	}, {
		init,
		fmt_default_done,
		fmt_default_reset,
		prepare,
		valid,
		split,
		get_binary,
		get_salt,
		fmt_default_source,
		{
			binary_hash_0,
			binary_hash_1,
			binary_hash_2,
			binary_hash_3,
			binary_hash_4,
			binary_hash_5,
			binary_hash_6
		},
		salt_hash,
		set_salt,
		netntlm_set_key,
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
