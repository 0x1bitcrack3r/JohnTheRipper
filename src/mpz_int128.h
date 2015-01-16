/*
 * Mimic integer (mpz) part of GMP using int128 macros, for speed.
 *
 * This software is Copyright (c) 2015 magnum
 * and is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * WARNING: Currently only functions needed for princeprocessor are
 * implemented, and some behavior differs from GMP (eg. return of
 * mpz_fdiv_q_ui() should return remainder but that's not currently used
 * by princeprocessor).
 *
 * WARNING 2: This is a hack. If using this somewhere else without verifying
 * functionality against real GMP, you may get totally unexpected behavior.
 * You have been warned.
 */

#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#undef int128_t
#define int128_t our_int128_t
#undef uint128_t
#define uint128_t our_uint128_t

#if HAVE___INT128
typedef __int128                int128_t;
typedef unsigned __int128       uint128_t;
#elif HAVE_INT128
typedef int128                  int128_t;
typedef unsigned int128         uint128_t;
#else
typedef __int128_t              int128_t;
typedef __uint128_t             uint128_t;
#endif

typedef uint128_t               mpz_t;
typedef double                  mpf_t;

#define mpz_init(x) x = 0
#define mpz_init_set(x, y) x = y
#define mpz_init_set_ui mpz_init_set
#define mpz_init_set_si mpz_init_set

#define mpz_clear(x) x = 0

#define mpz_set(rop, op) rop = op
#define mpz_set_ui mpz_set
#define mpz_set_si mpz_set

#define mpz_cmp(op1, op2) ((op1 > (op2)) ? 1 : (op1 < (op2)) ? -1 : 0)
#define mpz_cmp_ui mpz_cmp
#define mpz_cmp_si(op1, op2) (op1 - (op2))

#define mpz_add(rop, op1, op2) rop = (op1) + (op2)
#define mpz_add_ui mpz_add

#define mpz_sub(rop, op1, op2) rop = (op1) - (op2)
#define mpz_sub_ui mpz_sub

#define mpz_mul(rop, op1, op2) rop = (op1) * (op2)
#define mpz_mul_ui mpz_mul
#define mpz_mul_2exp(rop, op1, op2) rop = op1 << (op2)

#define mpz_div_ui(q, n, d) q = (n) / (d)
#define mpz_fdiv_ui(n, d) ((n) % (d))
#define mpz_fdiv_r_2exp(q, n, d) q = n & (((uint128_t)1 << (d)) - 1)
#define mpz_fdiv_q_2exp(q, n, d) q = n >> (d)

#define mpz_get_ui(op) op

#if 1
#define mpz_fdiv_q_ui(q, n, d) q = (n) / (d)
#else
#define mpz_fdiv_q_ui(q, n, d) _mpz_fdiv_q_ui(&q, n, d)
static inline int _mpz_fdiv_q_ui(mpz_t *q, mpz_t n, mpz_t d)
{
	*q = n / d;
	return n % d;
}
#endif

#define mpz_set_str(rop, str, base) _mpz_set_str(&rop, str, base)
static inline int _mpz_set_str(mpz_t *rop, char *str, int base)
{
	int num, ret = 0;

	if (base == 0 && str[0] != '0')
		base = 10;

	if (base != 10) {
		fprintf(stderr, "%s(): base %d not implemented\n",
		        __FUNCTION__, base);
		exit (EXIT_FAILURE);
	}

	if (strlen(str) != strspn(str, "0123456789 \t\n\v\f\r"))
		ret = 1;

	*rop = 0;
	while ((num = *str++)) {
		if (isspace(num))
			continue;
		*rop *=10;
		*rop += num - '0';
	}
	return ret;
}

/* This is slow and can't print '0'... but it's simple :-P */
static inline void print128(mpz_t op, FILE *stream)
{
	if (op == 0) {
		return;
	}

	print128(op / 10, stream);
	fputc(op % 10 + '0', stream);
}

static inline size_t mpz_out_str(FILE *stream, int base, mpz_t op)
{
	if (base != 10) {
		fprintf(stderr, "%s(): base %d not implemented\n",
		        __FUNCTION__, base);
		exit (EXIT_FAILURE);
	}

	if (op == 0)
		fputc('0', stream);
	else
		print128(op, stream);

	/* The GMP function returns number of characters written */
	return 1;
}

/* For JtR ETA/Progress compatibility */
#define mpf_init(x) x = 0
#define mpf_init_set_ui(x, y) x = y
#define mpf_set_z(x, y) x = y
#define mpf_div(q, n, d) q = (n) / (d)
#define mpf_clear(x) x = 0
#define mpf_get_d(x) x

/* Fugly but short :-P and only supports base 10 right now */
#define mpz_get_str(ptr, base, op) \
  do { \
    if (!op) \
      strcpy(ptr, "0"); \
    else \
      _int128tostr(op, ptr); \
  } while (0)

static inline int _int128tostr(uint128_t op, char *ptr)
{
  char *orig = ptr;
  if (op == 0)
    return 0;

  ptr += _int128tostr(op / 10, ptr);
  *ptr++ = op % 10 + '0';
  *ptr = 0;
  return ptr - orig;
}

#if 0
int main(int argc, char **argv)
{
	uint128_t a;

	if (argc <= 1)
		return EXIT_FAILURE;

	while (--argc) {
		mpz_set_str(a, *(++argv), 10);
		mpz_out_str(stderr, 10, a);
		fputc('\n', stderr);
	}
	return EXIT_SUCCESS;
}
#endif
