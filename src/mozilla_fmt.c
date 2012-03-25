/* Mozilla cracker patch for JtR. Hacked together during March of 2012 by
 * Dhiru Kholia <dhiru.kholia at gmail.com>
 *
 * Uses code from FireMasterLinux project.
 * (http://code.google.com/p/rainbowsandpwnies/wiki/FiremasterLinux) */

#ifdef HAVE_NSS
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "arch.h"
#include "misc.h"
#include "common.h"
#include "formats.h"
#include "params.h"
#include "options.h"
#include <openssl/sha.h>
#include "lowpbe.h"
#include "KeyDBCracker.h"

#define FORMAT_LABEL		"mozilla"
#define FORMAT_NAME		"Mozilla"
#define ALGORITHM_NAME		"32/" ARCH_BITS_STR
#define BENCHMARK_COMMENT	""
#define BENCHMARK_LENGTH	-1
#define PLAINTEXT_LENGTH	16
#define BINARY_SIZE		16
#define SALT_SIZE		512
#define MIN_KEYS_PER_CRYPT	1
#define MAX_KEYS_PER_CRYPT	1

static char saved_key[PLAINTEXT_LENGTH + 1];
static int cracked;
static SHA_CTX pctx;
static unsigned char encString[128];
struct NSSPKCS5PBEParameter *paramPKCS5 = NULL;
struct KeyCrackData keyCrackData;

static int CheckMasterPassword(char *password)
{
	unsigned char passwordHash[SHA1_LENGTH+1];
	SHA_CTX ctx;
	// Copy already calculated partial hash data..
	memcpy(&ctx, &pctx, sizeof(SHA_CTX) );
	SHA1_Update(&ctx, (unsigned char *)password, strlen(password));
	SHA1_Final(passwordHash, &ctx);
	return nsspkcs5_CipherData(paramPKCS5, passwordHash, encString);
}

static void init(struct fmt_main *pFmt)
{

}

static int valid(char *ciphertext, struct fmt_main *pFmt)
{
	return !strncmp(ciphertext, "$mozilla$", 9);
}

static void *get_salt(char *ciphertext)
{
	return ciphertext;
}


static void set_salt(void *salt)
{
	char *saltcopy = strdup(salt);
	char *keeptr = saltcopy;
	static char path[4096];
	saltcopy += 9;	/* skip over "$mozilla$*" */
	char *p = strtok(saltcopy, "*");
	strcpy(path, p);

   	SECItem saltItem;
        if(CrackKeyData(path, &keyCrackData) == false) {
                exit(0);
        }
        // initialize the pkcs5 structure
        saltItem.type = (SECItemType) 0;
        saltItem.len  = keyCrackData.saltLen;
        saltItem.data = keyCrackData.salt;
        paramPKCS5 = nsspkcs5_NewParam(0, &saltItem, 1);
        if(paramPKCS5 == NULL) {
                fprintf(stderr, "\nFailed to initialize NSSPKCS5 structure");
                exit(0);
        }

        // Current algorithm is
        // SEC_OID_PKCS12_PBE_WITH_SHA1_AND_TRIPLE_DES_CBC
        // Setup the encrypted password-check string
	memcpy(encString, keyCrackData.encData, keyCrackData.encDataLen );
	if(CheckMasterPassword("") == true ) {
		fprintf(stderr, "%s : Master Password is not set\n", (char *)salt);
        }

        // Calculate partial sha1 data for password hashing
	SHA1_Init(&pctx);
	SHA1_Update(&pctx, keyCrackData.globalSalt, keyCrackData.globalSaltLen);

	cracked = 0;
	free(keeptr);
}

static void crypt_all(int count)
{
	if(CheckMasterPassword(saved_key)) {
		cracked = 1;
	}
}

static int cmp_all(void *binary, int count)
{
	return cracked;
}

static int cmp_one(void *binary, int index)
{
	return cracked;
}

static int cmp_exact(char *source, int index)
{
    return 1;
}

static void mozilla_set_key(char *key, int index)
{
	int saved_key_length = strlen(key);
	if (saved_key_length > PLAINTEXT_LENGTH)
		saved_key_length = PLAINTEXT_LENGTH;
	memcpy(saved_key, key, saved_key_length);
	saved_key[saved_key_length] = 0;
}

static char *get_key(int index)
{
	return saved_key;
}

struct fmt_main mozilla_fmt = {
	{
		FORMAT_LABEL,
		FORMAT_NAME,
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		PLAINTEXT_LENGTH,
		BINARY_SIZE,
		SALT_SIZE,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
		FMT_CASE | FMT_8_BIT,
		NULL
	}, {
		init,
		fmt_default_prepare,
		valid,
		fmt_default_split,
		fmt_default_binary,
		get_salt,
		{
			fmt_default_binary_hash
		},
		fmt_default_salt_hash,
		set_salt,
		mozilla_set_key,
		get_key,
		fmt_default_clear_keys,
		crypt_all,
		{
			fmt_default_get_hash
		},
		cmp_all,
		cmp_one,
		cmp_exact
	}
};
#else
#ifdef __GNUC__
#warning Note: Mozilla format disabled, un-comment HAVE_NSS in Makefile if you have NSS installed.
#endif
#endif
