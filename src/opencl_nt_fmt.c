/* NTLM patch for john (performance improvement and OpenCL 1.0 conformant)
 *
 * Written by Alain Espinosa <alainesp at gmail.com> in 2010 and modified
 * by Samuele Giovanni Tonon in 2011.  No copyright is claimed, and
 * the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the
 * public domain is deemed null and void, then the software is
 * Copyright (c) 2010 Alain Espinosa
 * Copyright (c) 2011 Samuele Giovanni Tonon
 * Copyright (c) 2013 Sayantan Datta
 * and it is hereby released to the general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 *
 * (This is a heavily cut-down "BSD license".)
 */

#include <string.h>
#include <math.h>

#include "arch.h"
#include "misc.h"
#include "options.h"
#include "memory.h"
#include "common.h"
#include "formats.h"
#include "path.h"
#include "loader.h"
#include "common-opencl.h"
#include "opencl_nt_fmt.h"

#define FORMAT_LABEL		"nt-opencl"
#define BUFSIZE             	((PLAINTEXT_LENGTH+3)/4*4)
#define FORMAT_NAME		"NT"
#define ALGORITHM_NAME		"MD4 OpenCL (inefficient, development use only)"
#define BENCHMARK_COMMENT	""
#define BENCHMARK_LENGTH	-1
#define PLAINTEXT_LENGTH	23
#define CIPHERTEXT_LENGTH	32
#define BINARY_SIZE		16
#define BINARY_ALIGN		4
#define SALT_SIZE		0

//2^10 * 2^9
#define MIN_KEYS_PER_CRYPT	(1024*2048*2)
#define MAX_KEYS_PER_CRYPT	MIN_KEYS_PER_CRYPT

#define OCL_CONFIG		"nt"

static struct fmt_tests tests[] = {
	{"b7e4b9022cd45f275334bbdb83bb5be5", "John the Ripper"},
	{"$NT$8bd6e4fb88e01009818749c5443ea712", "\xFC"},         // German u-diaeresis in ISO-8859-1
	{"$NT$cc1260adb6985ca749f150c7e0b22063", "\xFC\xFC"},     // Two of the above
	{"$NT$72810BFD51F61B92956CE08E22FD6C74", "abcdefghijklmnopqrstuvw"}, //Max length password
	{"$NT$f9e37e83b83c47a93c2f09f66408631b", "abc123"},
	{"$NT$8846f7eaee8fb117ad06bdd830b7586c", "password"},
	{"$NT$2b2ac2d1c7c8fda6cea80b5fad7563aa", "computer"},
	{"$NT$32ed87bdb5fdc5e9cba88547376818d4", "123456"},
	{"$NT$b7e0ea9fbffcf6dd83086e905089effd", "tigger"},
	{"$NT$7ce21f17c0aee7fb9ceba532d0546ad6", "1234"},
	{"$NT$b23a90d0aad9da3615fafc27a1b8baeb", "a1b2c3"},
	{"$NT$2d20d252a479f485cdf5e171d93985bf", "qwerty"},
	{"$NT$3dbde697d71690a769204beb12283678", "123"},
	{"$NT$c889c75b7c1aae1f7150c5681136e70e", "xxx"},
	{"$NT$d5173c778e0f56d9fc47e3b3c829aca7", "money"},
	{"$NT$0cb6948805f797bf2a82807973b89537", "test"},
	{"$NT$0569fcf2b14b9c7f3d3b5f080cbd85e5", "carmen"},
	{"$NT$f09ab1733a528f430353834152c8a90e", "mickey"},
	{"$NT$878d8014606cda29677a44efa1353fc7", "secret"},
	{"$NT$85ac333bbfcbaa62ba9f8afb76f06268", "summer"},
	{"$NT$5962cc080506d90be8943118f968e164", "internet"},
	{"$NT$f07206c3869bda5acd38a3d923a95d2a", "service"},
	{"$NT$31d6cfe0d16ae931b73c59d7e0c089c0", ""},
	{"$NT$d0dfc65e8f286ef82f6b172789a0ae1c", "canada"},
	{"$NT$066ddfd4ef0e9cd7c256fe77191ef43c", "hello"},
	{"$NT$39b8620e745b8aa4d1108e22f74f29e2", "ranger"},
	{"$NT$8d4ef8654a9adc66d4f628e94f66e31b", "shadow"},
	{"$NT$320a78179516c385e35a93ffa0b1c4ac", "baseball"},
	{"$NT$e533d171ac592a4e70498a58b854717c", "donald"},
	{"$NT$5eee54ce19b97c11fd02e531dd268b4c", "harley"},
	{"$NT$6241f038703cbfb7cc837e3ee04f0f6b", "hockey"},
	{"$NT$becedb42ec3c5c7f965255338be4453c", "letmein"},
	{"$NT$ec2c9f3346af1fb8e4ee94f286bac5ad", "maggie"},
	{"$NT$f5794cbd75cf43d1eb21fad565c7e21c", "mike"},
	{"$NT$74ed32086b1317b742c3a92148df1019", "mustang"},
	{"$NT$63af6e1f1dd9ecd82f17d37881cb92e6", "snoopy"},
	{"$NT$58def5844fe58e8f26a65fff9deb3827", "buster"},
	{"$NT$f7eb9c06fafaa23c4bcf22ba6781c1e2", "dragon"},
	{"$NT$dd555241a4321657e8b827a40b67dd4a", "jordan"},
	{"$NT$bb53a477af18526ada697ce2e51f76b3", "michael"},
	{"$NT$92b7b06bb313bf666640c5a1e75e0c18", "michelle"},
	{NULL}
};

//Init values
#define INIT_A 0x67452301
#define INIT_B 0xefcdab89
#define INIT_C 0x98badcfe
#define INIT_D 0x10325476

#define SQRT_2 0x5a827999
#define SQRT_3 0x6ed9eba1

cl_mem 	pinned_saved_keys, pinned_saved_idx, pinned_bbbs, buffer_out, buffer_keys, buffer_idx;
static cl_uint *bbbs, *res_hashes;
static unsigned int *saved_plain;
static uint64_t *saved_idx, key_idx = 0;
static int max_key_length = 0;
static unsigned int num_keys = 0;
static int have_full_hashes;

cl_mem buffer_ld_hashes, buffer_outKeyIdx;
cl_mem buffer_bitmap1, buffer_bitmap2;
static unsigned int *loaded_hashes, loaded_count, cmp_out, *outKeyIdx;
static struct bitmap_context_mixed *bitmap1;
static struct bitmap_context_global *bitmap2;

cl_mem  buffer_mask_gpu;
static struct mask_context msk_ctx;
static struct db_main *DB;
static unsigned char *mask_offsets;
static unsigned int multiplier = 1;

static unsigned int mask_mode = 0;
static unsigned int benchmark = 1;

cl_kernel crk_kernel, crk_kernel_nnn, crk_kernel_cnn, crk_kernel_ccc, crk_kernel_om, zero;

static int crypt_all_self_test(int *pcount, struct db_salt *_salt);
static int crypt_all(int *pcount, struct db_salt *_salt);
static void load_mask(struct fmt_main *fmt);
static char *get_key_self_test(int index);
static char *get_key(int index);

static void release_clobj(void)
{
	if (benchmark)
		HANDLE_CLERROR(clEnqueueUnmapMemObject(queue[ocl_gpu_id], pinned_bbbs, bbbs, 0,NULL,NULL), "Error Unmapping partial_hashes");
	else
		MEM_FREE(bbbs);

	clEnqueueUnmapMemObject(queue[ocl_gpu_id], pinned_saved_keys, saved_plain, 0, NULL, NULL);

        HANDLE_CLERROR(clReleaseMemObject(buffer_keys), "Release mem in");
	HANDLE_CLERROR(clReleaseMemObject(buffer_idx), "Release buffer idx");
	HANDLE_CLERROR(clReleaseMemObject(buffer_out), "Release mem setting");
	HANDLE_CLERROR(clReleaseMemObject(pinned_bbbs), "Release mem out");
        HANDLE_CLERROR(clReleaseMemObject(pinned_saved_keys), "Release mem out");
	HANDLE_CLERROR(clReleaseMemObject(pinned_saved_idx), "Release mem out");
	HANDLE_CLERROR(clReleaseMemObject(buffer_mask_gpu), "Release mask");

	MEM_FREE(res_hashes);

	if(!benchmark) {

		MEM_FREE(loaded_hashes);
		MEM_FREE(outKeyIdx);
		MEM_FREE(mask_offsets);
		MEM_FREE(bitmap1);
		MEM_FREE(bitmap2);

		HANDLE_CLERROR(clReleaseMemObject(buffer_ld_hashes), "Release loaded hashes");
		HANDLE_CLERROR(clReleaseMemObject(buffer_outKeyIdx), "Release output key indeces");
		HANDLE_CLERROR(clReleaseMemObject(buffer_bitmap1), "Release bitmap1");
		HANDLE_CLERROR(clReleaseMemObject(buffer_bitmap2), "Release bitmap2");
	}
}

static void done(void)
{
	release_clobj();

	HANDLE_CLERROR(clReleaseKernel(crypt_kernel), "Release kernel self_test");
	HANDLE_CLERROR(clReleaseKernel(crk_kernel_nnn), "Release kernel mask mode nnn");
	HANDLE_CLERROR(clReleaseKernel(crk_kernel_cnn), "Release kernel mask mode cnn");
	HANDLE_CLERROR(clReleaseKernel(crk_kernel_ccc), "Release kernel mask mode ccc");
	HANDLE_CLERROR(clReleaseKernel(crk_kernel_om), "Release kernel non-mask mode");
	HANDLE_CLERROR(clReleaseKernel(zero), "Release kernel zero");
	HANDLE_CLERROR(clReleaseProgram(program[ocl_gpu_id]), "Release Program");
}

/* crk_kernel_ccc: optimized for kernel with all 3 ranges consecutive.
 * crk_kernel_nnn: optimized for kernel with no consecutive ranges.
 * crk_kernel_cnn: optimized for kernel with 1st range being consecutive and remaining ranges non-consecutive.
 *
 * select_kernel() assumes that the active ranges are arranged according to decreasing character count, which is taken
 * care of inside check_mask_rawmd5().
 *
 * crk_kernel_ccc used for mask types: ccc, cc, c.
 * crk_kernel_nnn used for mask types: nnn, nnc, ncn, ncc, nc, nn, n.
 * crk_kernel_cnn used for mask types: cnn, cnc, ccn, cn.
 */

static void select_kernel(struct mask_context *msk_ctx) {

	if (!(msk_ctx->ranges[msk_ctx->activeRangePos[0]].start)) {
		crk_kernel = crk_kernel_nnn;
		fprintf(stderr,"Using kernel nt_nnn...\n" );
		return;
	}

	else {
		crk_kernel = crk_kernel_ccc;

		if ((msk_ctx->count) > 1) {
			if (!(msk_ctx->ranges[msk_ctx->activeRangePos[1]].start)) {
				crk_kernel = crk_kernel_cnn;
				fprintf(stderr,"Using kernel nt_cnn...\n" );
				return;
			}

			else {
				crk_kernel = crk_kernel_ccc;

				/* For type ccn */
				if ((msk_ctx->count) == 3)
					if (!(msk_ctx->ranges[msk_ctx->activeRangePos[2]].start))  {
						crk_kernel = crk_kernel_cnn;
						if ((msk_ctx->ranges[msk_ctx->activeRangePos[2]].count) > 64) {
							fprintf(stderr,"nt-opencl failed processing mask type ccn.\n" );
						}
						fprintf(stderr,"Using kernel nt_cnn...\n" );
						return;
					}

				fprintf(stderr,"Using kernel nt_ccc...\n" );
				return;
			}
		}

		fprintf(stderr,"Using kernel nt_ccc...\n" );
		return;
	}
}

// TODO: Use concurrent memory copy & execute

static void init(struct fmt_main *self){
	int argIndex = 0;
	cl_ulong maxsize;

	opencl_init("$JOHN/kernels/nt_kernel.cl", ocl_gpu_id, NULL);

	/* Read LWS/GWS prefs from config or environment */
	opencl_get_user_preferences(OCL_CONFIG);

	if (!local_work_size)
		local_work_size = LWS;

	if (!global_work_size)
		global_work_size = MAX_KEYS_PER_CRYPT;

	if(global_work_size < 1024) global_work_size = 1024;
	/* Round off to nearest power of 2 */
	local_work_size = pow(2, ceil(log(local_work_size)/log(2)));

	crypt_kernel = clCreateKernel( program[ocl_gpu_id], "nt_self_test", &ret_code );
	HANDLE_CLERROR(ret_code,"Error creating kernel");

	crk_kernel_nnn = clCreateKernel( program[ocl_gpu_id], "nt_nnn", &ret_code);
	HANDLE_CLERROR(ret_code, "Error creating kernel nnn");

	crk_kernel_cnn = clCreateKernel( program[ocl_gpu_id], "nt_cnn", &ret_code);
	HANDLE_CLERROR(ret_code, "Error creating kernel cnn");

	crk_kernel_ccc = clCreateKernel( program[ocl_gpu_id], "nt_ccc", &ret_code);
	HANDLE_CLERROR(ret_code, "Error creating kernel ccc");

	crk_kernel_om = clCreateKernel( program[ocl_gpu_id], "nt_om", &ret_code );
	HANDLE_CLERROR(ret_code, "Error creating kernel");

	zero = clCreateKernel(program[ocl_gpu_id], "zero", &ret_code);
	HANDLE_CLERROR(ret_code, "Error creating kernel. Double-check kernel name?");

	/* Note: we ask for the kernel's max size, not the device's! */
	maxsize = get_kernel_max_lws(ocl_gpu_id, crypt_kernel);

	while (local_work_size > maxsize)
		local_work_size >>= 1;

	pinned_bbbs = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR,4*global_work_size, NULL, &ret_code);
	HANDLE_CLERROR(ret_code,"Error creating page-locked memory");

	res_hashes = mem_alloc(sizeof(cl_uint) * 3 * global_work_size);
	HANDLE_CLERROR(ret_code,"Error mapping page-locked memory");
	bbbs = (cl_uint*)clEnqueueMapBuffer(queue[ocl_gpu_id], pinned_bbbs , CL_TRUE, CL_MAP_READ, 0, 4*global_work_size, 0, NULL, NULL, &ret_code);
	HANDLE_CLERROR(ret_code,"Error mapping page-locked memory");

	buffer_keys = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_ONLY, BUFSIZE * global_work_size, NULL, &ret_code);
	HANDLE_CLERROR(ret_code, "Error creating buffer argument buffer_keys");
	buffer_idx = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_ONLY, sizeof(uint64_t) * global_work_size, NULL, &ret_code);
	HANDLE_CLERROR(ret_code, "Error creating buffer argument buffer_idx");
	buffer_out  = clCreateBuffer( context[ocl_gpu_id], CL_MEM_WRITE_ONLY , 4*4*global_work_size, NULL, &ret_code );
	HANDLE_CLERROR(ret_code,"Error creating buffer argument");
	buffer_mask_gpu = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_ONLY, sizeof(struct mask_context) , NULL, &ret_code);
	HANDLE_CLERROR(ret_code, "Error creating buffer mask gpu\n");

	pinned_saved_keys = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR, BUFSIZE * global_work_size, NULL, &ret_code);
	HANDLE_CLERROR(ret_code, "Error creating page-locked memory pinned_saved_keys");
	saved_plain = clEnqueueMapBuffer(queue[ocl_gpu_id], pinned_saved_keys, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0, BUFSIZE * global_work_size, 0, NULL, NULL, &ret_code);
	HANDLE_CLERROR(ret_code, "Error mapping page-locked memory saved_plain");

	pinned_saved_idx = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR, sizeof(uint64_t) * global_work_size, NULL, &ret_code);
	HANDLE_CLERROR(ret_code, "Error creating page-locked memory pinned_saved_idx");
	saved_idx = clEnqueueMapBuffer(queue[ocl_gpu_id], pinned_saved_idx, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0, sizeof(uint64_t) * global_work_size, 0, NULL, NULL, &ret_code);
	HANDLE_CLERROR(ret_code, "Error mapping page-locked memory saved_idx");

	argIndex = 0;

	HANDLE_CLERROR(clSetKernelArg(crypt_kernel, argIndex++, sizeof(buffer_keys), (void*) &buffer_keys),
		"Error setting argument 0");
	HANDLE_CLERROR(clSetKernelArg(crypt_kernel, argIndex++, sizeof(buffer_idx), (void*) &buffer_idx),
		"Error setting argument 1");
	HANDLE_CLERROR(clSetKernelArg(crypt_kernel, argIndex++, sizeof(buffer_out ), (void*) &buffer_out ),
		"Error setting argument 2");

	/* This format can't run with reduced global work size */
	self->params.min_keys_per_crypt = global_work_size;
	self->params.max_keys_per_crypt = global_work_size;
	if (!local_work_size)
		local_work_size = LWS;

	if(options.mask) {
		int i;
		mask_mode = 1;
		load_mask(self);
		multiplier = 1;
		for (i = 0; i < msk_ctx.count; i++)
			multiplier *= msk_ctx.ranges[msk_ctx.activeRangePos[i]].count;
	}

	if (options.verbosity > 2)
		fprintf(stderr, "Local worksize (LWS) %d, Global worksize (GWS) %d\n", (int)local_work_size, (int)global_work_size);

	self->methods.crypt_all = crypt_all_self_test;
	self->methods.get_key = get_key_self_test;
}

static char *split(char *ciphertext, int index, struct fmt_main *self)
{
	static char out[CIPHERTEXT_LENGTH + 4 + 1];

	if (!strncmp(ciphertext, "$NT$", 4))
		ciphertext += 4;

	out[0] = '$';
	out[1] = 'N';
	out[2] = 'T';
	out[3] = '$';

	memcpy(&out[4], ciphertext, 32);
	out[36] = 0;

	strlwr(&out[4]);

	return out;
}

static int valid(char *ciphertext, struct fmt_main *self)
{
        char *pos;

	if (!strncmp(ciphertext, "$NT$", 4))
		ciphertext += 4;

        for (pos = ciphertext; atoi16[ARCH_INDEX(*pos)] != 0x7F; pos++);

        if (!*pos && pos - ciphertext == CIPHERTEXT_LENGTH)
		return 1;
        else
	return 0;

}

// here to 'handle' the pwdump files:  user:uid:lmhash:ntlmhash:::
// Note, we address the user id inside loader.
static char *prepare(char *split_fields[10], struct fmt_main *self)
{
	static char out[33+5];

	if (!valid(split_fields[1], self)) {
		if (split_fields[3] && strlen(split_fields[3]) == 32) {
			sprintf(out, "$NT$%s", split_fields[3]);
			if (valid(out,self))
				return out;
		}
	}
	return split_fields[1];
}

static void *get_binary(char *ciphertext)
{
	static unsigned int out[4];
	unsigned int i=0;
	unsigned int temp;

	ciphertext+=4;
	for (; i<4; i++){
		temp  = (atoi16[ARCH_INDEX(ciphertext[i*8+0])])<<4;
		temp |= (atoi16[ARCH_INDEX(ciphertext[i*8+1])]);

		temp |= (atoi16[ARCH_INDEX(ciphertext[i*8+2])])<<12;
		temp |= (atoi16[ARCH_INDEX(ciphertext[i*8+3])])<<8;

		temp |= (atoi16[ARCH_INDEX(ciphertext[i*8+4])])<<20;
		temp |= (atoi16[ARCH_INDEX(ciphertext[i*8+5])])<<16;

		temp |= (atoi16[ARCH_INDEX(ciphertext[i*8+6])])<<28;
		temp |= (atoi16[ARCH_INDEX(ciphertext[i*8+7])])<<24;

		out[i]=temp;
	}

	out[0] -= INIT_A;
	out[1] -= INIT_B;
	out[2] -= INIT_C;
	out[3] -= INIT_D;

	out[1]  = (out[1] >> 15) | (out[1] << 17);
	out[1] -= SQRT_3 + (out[2] ^ out[3] ^ out[0]);
	out[1]  = (out[1] >> 15) | (out[1] << 17);
	out[1] -= SQRT_3;

	return out;
}

static int binary_hash_0(void *binary) { return ((unsigned int *)binary)[1] & 0xF; }
static int binary_hash_1(void *binary) { return ((unsigned int *)binary)[1] & 0xFF; }
static int binary_hash_2(void *binary) { return ((unsigned int *)binary)[1] & 0xFFF; }
static int binary_hash_3(void *binary) { return ((unsigned int *)binary)[1] & 0xFFFF; }
static int binary_hash_4(void *binary) { return ((unsigned int *)binary)[1] & 0xFFFFF; }
static int binary_hash_5(void *binary) { return ((unsigned int *)binary)[1] & 0xFFFFFF; }
static int binary_hash_6(void *binary) { return ((unsigned int *)binary)[1] & 0x7FFFFFF; }

static int get_hash_0(int index) { return bbbs[index] & 0xF; }
static int get_hash_1(int index) { return bbbs[index] & 0xFF; }
static int get_hash_2(int index) { return bbbs[index] & 0xFFF; }
static int get_hash_3(int index) { return bbbs[index] & 0xFFFF; }
static int get_hash_4(int index) { return bbbs[index] & 0xFFFFF; }
static int get_hash_5(int index) { return bbbs[index] & 0xFFFFFF; }
static int get_hash_6(int index) { return bbbs[index] & 0x7FFFFFF; }

static int cmp_all(void *binary, int count) {
	if(benchmark) {
		unsigned int i=0;
		unsigned int b=((unsigned int *)binary)[1];

		for(;i<count;i++)
			if(b==bbbs[i])
				return 1;
		return 0;
	}
	else return 1;
}

static int cmp_one(void * binary, int index)
{
	unsigned int *t=(unsigned int *)binary;
	if (t[1]==bbbs[index])
		return 1;
	return 0;

}

static int cmp_exact(char *source, int index) {

	if(benchmark || cmp_out) {
		unsigned int *t = (unsigned int *) get_binary(source);
		unsigned int num = benchmark ? global_work_size: loaded_count;
		if(benchmark) {
			if (!have_full_hashes){
				clEnqueueReadBuffer(queue[ocl_gpu_id], buffer_out, CL_TRUE,
					sizeof(cl_uint) * num,
					sizeof(cl_uint) * 3 * num, res_hashes, 0,
					NULL, NULL);
				have_full_hashes = 1;
			}

			if (t[0]!=res_hashes[index])
				return 0;
			if (t[2]!=res_hashes[1 * num + index])
				return 0;
			if (t[3]!=res_hashes[2 * num + index])
				return 0;
			return 1;
		}
		else {
			if(!outKeyIdx[index]) return 0;
			if (t[0]!=loaded_hashes[index + 1])
				return 0;
			if (t[2]!=loaded_hashes[2 * num + index +1])
				return 0;
			if (t[3]!=loaded_hashes[3 * num + index + 1])
				return 0;
			return 1;
		}
	}

	return 0;
}

static void setKernelArgs(cl_kernel *kernel) {
	int argIndex = 0;

	HANDLE_CLERROR(clSetKernelArg(*kernel, argIndex++, sizeof(buffer_keys), (void*) &buffer_keys),
		"Error setting argument 0");
	HANDLE_CLERROR(clSetKernelArg(*kernel, argIndex++, sizeof(buffer_idx), (void*) &buffer_idx),
		"Error setting argument 1");
	HANDLE_CLERROR(clSetKernelArg(*kernel, argIndex++, sizeof(buffer_ld_hashes), (void*) &buffer_ld_hashes ),
		"Error setting argument 2");
	HANDLE_CLERROR(clSetKernelArg(*kernel, argIndex++, sizeof(buffer_outKeyIdx), (void*) &buffer_outKeyIdx ),
		"Error setting argument 3");
	HANDLE_CLERROR(clSetKernelArg(*kernel, argIndex++, sizeof(buffer_bitmap1), (void*) &buffer_bitmap1 ),
		"Error setting argument 4");
	HANDLE_CLERROR(clSetKernelArg(*kernel, argIndex++, sizeof(buffer_bitmap2), (void*) &buffer_bitmap2 ),
		"Error setting argument 5");
	if(mask_mode)
		HANDLE_CLERROR(clSetKernelArg(*kernel, argIndex++, sizeof(buffer_mask_gpu), (void*) &buffer_mask_gpu),
			"Error setting argument 6");
	HANDLE_CLERROR(clSetKernelArg(zero, 0, sizeof(buffer_outKeyIdx), &buffer_outKeyIdx), "Error setting argument 0");
}

static void opencl_nt_reset(struct db_main *db) {

	if(db) {
		unsigned int length = 0;

		HANDLE_CLERROR(clEnqueueUnmapMemObject(queue[ocl_gpu_id], pinned_bbbs, bbbs, 0,NULL,NULL), "Error Unmapping partial_hashes");
		loaded_hashes = (unsigned int*)mem_alloc(((db->password_count) * 4 + 1)*sizeof(unsigned int));
		bbbs = (unsigned int*)mem_alloc(((db->password_count) + 1)*sizeof(unsigned int));
		outKeyIdx     = (unsigned int*)mem_calloc((db->password_count) * sizeof(unsigned int) * 2);
		mask_offsets  = (unsigned char*) mem_calloc(db->format->params.max_keys_per_crypt);
		bitmap1       = (struct bitmap_context_mixed*)mem_alloc(sizeof(struct bitmap_context_mixed));
		bitmap2       = (struct bitmap_context_global*)mem_alloc(sizeof(struct bitmap_context_global));

		buffer_ld_hashes = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_WRITE, ((db->password_count) * 4 + 1)*sizeof(int), NULL, &ret_code);
		HANDLE_CLERROR(ret_code, "Error creating buffer arg loaded_hashes\n");

		length = ((db->format->params.max_keys_per_crypt) > ((db->password_count) * sizeof(unsigned int) * 2)) ?
			  (db->format->params.max_keys_per_crypt) : ((db->password_count) * sizeof(unsigned int) * 2);
		/* buffer_outKeyIdx is multiplexed for use as mask_offset input and keyIdx output */
		buffer_outKeyIdx = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_WRITE, length, NULL, &ret_code);
		HANDLE_CLERROR(ret_code, "Error creating buffer cmp_out\n");

		buffer_bitmap1 = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_WRITE, sizeof(struct bitmap_context_mixed), NULL, &ret_code);
		HANDLE_CLERROR(ret_code, "Error creating buffer arg loaded_hashes\n");

		buffer_bitmap2 = clCreateBuffer(context[ocl_gpu_id], CL_MEM_READ_WRITE, sizeof(struct bitmap_context_global), NULL, &ret_code);
		HANDLE_CLERROR(ret_code, "Error creating buffer arg loaded_hashes\n");

		if(mask_mode) {
			setKernelArgs(&crk_kernel_nnn);
			setKernelArgs(&crk_kernel_ccc);
			setKernelArgs(&crk_kernel_cnn);
			select_kernel(&msk_ctx);
			DB = db;
		}
		else {
			setKernelArgs(&crk_kernel_om);
			crk_kernel = crk_kernel_om;
		}

		db->format->methods.crypt_all = crypt_all;
		db->format->methods.get_key = get_key;

		benchmark = 0;
	}
}

static void load_hash(struct db_salt *salt) {

	unsigned int *bin, i;
	struct db_password *pw;

	loaded_count = (salt->count);
	loaded_hashes[0] = loaded_count;
	pw = salt -> list;
	i = 0;
	do {
		bin = (unsigned int *)pw -> binary;
		// Potential segfault if removed
		if(bin != NULL) {
			loaded_hashes[i + 1] = bin[0];
			loaded_hashes[i + loaded_count + 1] = bin[1];
			loaded_hashes[i + 2 * loaded_count + 1] = bin[2];
			loaded_hashes[i + 3 * loaded_count + 1] = bin[3];
			i++ ;
		}
	} while ((pw = pw -> next)) ;

	if(i != (salt->count)) {
		fprintf(stderr, "Something went wrong while loading hashes to gpu..Exiting..\n");
		exit(0);
	}

	HANDLE_CLERROR(clEnqueueWriteBuffer(queue[ocl_gpu_id], buffer_ld_hashes, CL_TRUE, 0, (i * 4 + 1) * sizeof(unsigned int) , loaded_hashes, 0, NULL, NULL), "failed in clEnqueueWriteBuffer loaded_hashes");
}

static void load_bitmap(unsigned int num_loaded_hashes, unsigned int index, unsigned int *bitmap, size_t szBmp) {
	unsigned int i, hash;
	memset(bitmap, 0, szBmp);

	for(i = 0; i < num_loaded_hashes; i++) {
		hash = loaded_hashes[index * num_loaded_hashes + i + 1] & (szBmp * 8 - 1);
		// divide by 32 , harcoded here and correct only for unsigned int
		bitmap[hash >> 5] |= (1U << (hash & 31));
	}
}

static void load_hashtable_plus(unsigned int *hashtable, unsigned int *loaded_next_hash, unsigned int idx, unsigned int num_loaded_hashes, unsigned int szHashTbl) {
	unsigned int i;

	memset(hashtable, 0xFF, szHashTbl * sizeof(unsigned int));
	memset(loaded_next_hash, 0xFF, num_loaded_hashes * sizeof(unsigned int));

	for (i = 0; i < num_loaded_hashes; ++i) {
		unsigned int hash = loaded_hashes[i + idx*num_loaded_hashes + 1] & (szHashTbl - 1);
		loaded_next_hash[i] = hashtable[hash];

		hashtable[hash] = i;
	}
}

static void check_mask_nt(struct mask_context *msk_ctx) {
	int i, j, k ;

	if(msk_ctx -> count > PLAINTEXT_LENGTH) msk_ctx -> count = PLAINTEXT_LENGTH;
	if(msk_ctx -> count > MASK_RANGES_MAX) {
		fprintf(stderr, "MASK parameters are too small...Exiting...\n");
		exit(EXIT_FAILURE);

	}

  /* Assumes msk_ctx -> activeRangePos[] is sorted. Check if any range exceeds nt key limit */
	for( i = 0; i < msk_ctx->count; i++)
		if(msk_ctx->ranges[msk_ctx->activeRangePos[i]].pos >= PLAINTEXT_LENGTH) {
			msk_ctx->count = i;
			break;
		}
	j = 0;
	i = 0;
	k = 0;
 /* Append non-active portion to activeRangePos[] for ease of computation inside GPU */
	while((j <= msk_ctx -> activeRangePos[k]) && (k < msk_ctx -> count)) {
		if(j == msk_ctx -> activeRangePos[k]) {
			k++;
			j++;
			continue;
		}
		msk_ctx -> activeRangePos[msk_ctx -> count + i] = j;
		i++;
		j++;
	}
	while ((i+msk_ctx->count) < MASK_RANGES_MAX) {
		msk_ctx -> activeRangePos[msk_ctx -> count + i] = j;
		i++;
		j++;
	}

	for(i = msk_ctx->count; i < MASK_RANGES_MAX; i++)
		msk_ctx->ranges[msk_ctx -> activeRangePos[i]].count = 0;

	/* Sort active ranges in descending order of charchter count */
	if(msk_ctx->ranges[msk_ctx -> activeRangePos[0]].count < msk_ctx->ranges[msk_ctx -> activeRangePos[1]].count) {
		i = msk_ctx -> activeRangePos[1];
		msk_ctx -> activeRangePos[1] = msk_ctx -> activeRangePos[0];
		msk_ctx -> activeRangePos[0] = i;
	}

	if(msk_ctx->ranges[msk_ctx -> activeRangePos[0]].count < msk_ctx->ranges[msk_ctx -> activeRangePos[2]].count) {
		i = msk_ctx -> activeRangePos[2];
		msk_ctx -> activeRangePos[2] = msk_ctx -> activeRangePos[0];
		msk_ctx -> activeRangePos[0] = i;
	}

	if(msk_ctx->ranges[msk_ctx -> activeRangePos[1]].count < msk_ctx->ranges[msk_ctx -> activeRangePos[2]].count) {
		i = msk_ctx -> activeRangePos[2];
		msk_ctx -> activeRangePos[2] = msk_ctx -> activeRangePos[1];
		msk_ctx -> activeRangePos[1] = i;
	}

	/* Consecutive charchters in ranges that have all the charchters consective are
	 * arranged in ascending order. This is to make password generation on host and device
	 * match each other for kernels that have consecutive charchter optimizations.*/
	for( i = 0; i < msk_ctx->count; i++)
		if(msk_ctx->ranges[msk_ctx -> activeRangePos[i]].start != 0) {
			for (j = 0; j < msk_ctx->ranges[msk_ctx -> activeRangePos[i]].count; j++)
				msk_ctx->ranges[msk_ctx -> activeRangePos[i]].chars[j] =
					msk_ctx->ranges[msk_ctx -> activeRangePos[i]].start + j;
		}
}

static void load_mask(struct fmt_main *fmt) {

	if (!fmt->private.msk_ctx) {
		fprintf(stderr, "No given mask.Exiting...\n");
		exit(EXIT_FAILURE);
	}
	memcpy(&msk_ctx, fmt->private.msk_ctx, sizeof(struct mask_context));
	check_mask_nt(&msk_ctx);

	HANDLE_CLERROR(clEnqueueWriteBuffer(queue[ocl_gpu_id], buffer_mask_gpu, CL_TRUE, 0, sizeof(struct mask_context), &msk_ctx, 0, NULL, NULL ), "Failed Copy data to gpu");
}

static void set_key(char *_key, int index)
{
	//if(index == 25) fprintf(stderr, "Set_key:%s %d\n",_key, strlen(_key));
	const ARCH_WORD_32 *key = (ARCH_WORD_32*)_key;
	int len = strlen(_key);

	saved_idx[index] = (key_idx << 6) | len;

	while (len > 4) {
		saved_plain[key_idx++] = *key++;
		len -= 4;
	}
	if (len)
		saved_plain[key_idx++] = *key & (0xffffffffU >> (32 - (len << 3)));

	num_keys++;
}

static char *get_key_self_test(int index)
{
	static char out[PLAINTEXT_LENGTH + 20];
	int i;
	int  len = saved_idx[index] & 63;
	char *key = (char*)&saved_plain[saved_idx[index] >> 6];

	for (i = 0; i < len; i++)
		out[i] = *key++;
	out[i] = 0;

	return out;
}

static void passgen(int ctr, int offset, char *key) {
	int i, j, k;

	offset = msk_ctx.flg_wrd ? offset : 0;

	i =  ctr % msk_ctx.ranges[msk_ctx.activeRangePos[0]].count;
	key[msk_ctx.ranges[msk_ctx.activeRangePos[0]].pos + offset] = msk_ctx.ranges[msk_ctx.activeRangePos[0]].chars[i];

	if (msk_ctx.ranges[msk_ctx.activeRangePos[1]].count) {
		j = (ctr / msk_ctx.ranges[msk_ctx.activeRangePos[0]].count) % msk_ctx.ranges[msk_ctx.activeRangePos[1]].count;
		key[msk_ctx.ranges[msk_ctx.activeRangePos[1]].pos + offset] = msk_ctx.ranges[msk_ctx.activeRangePos[1]].chars[j];
	}
	if (msk_ctx.ranges[msk_ctx.activeRangePos[2]].count) {
		k = (ctr / (msk_ctx.ranges[msk_ctx.activeRangePos[0]].count * msk_ctx.ranges[msk_ctx.activeRangePos[1]].count)) % msk_ctx.ranges[msk_ctx.activeRangePos[2]].count;
		key[msk_ctx.ranges[msk_ctx.activeRangePos[2]].pos + offset] = msk_ctx.ranges[msk_ctx.activeRangePos[2]].chars[k];
	}
}

static char *get_key(int index)
{
	static char out[PLAINTEXT_LENGTH + 1];
	int i;
	int  len, ctr = 0, mask_offset = 0, flag = 0;
	char *key;

	if((index < loaded_count) && cmp_out) {
		ctr = outKeyIdx[index + loaded_count];
		/* outKeyIdx contains all zero when no new passwords are cracked.
		 * Hence during status checks even if index is less than loaded count
		 * correct range of passwords is displayed.
		 */
		index = outKeyIdx[index] & 0x7fffffff;
		mask_offset = mask_offsets[index];
		flag = 1;
	}
	index = (index > num_keys)? (num_keys?num_keys-1:0): index;
	len = saved_idx[index] & 63;
	key = (char*)&saved_plain[saved_idx[index] >> 6];

	for (i = 0; i < len; i++)
		out[i] = *key++;

	if(cmp_out && mask_mode && flag)
		passgen(ctr, mask_offset, out);

	out[i] = 0;

	return out;
}

static int crypt_all_self_test(int *pcount, struct db_salt *salt)
{
	int count = *pcount;

	// copy keys to the device
	HANDLE_CLERROR(clEnqueueWriteBuffer(queue[ocl_gpu_id], buffer_keys, CL_TRUE, 0, 4 * key_idx, saved_plain, 0, NULL, NULL), "failed in clEnqueueWriteBuffer buffer_keys");
	HANDLE_CLERROR(clEnqueueWriteBuffer(queue[ocl_gpu_id], buffer_idx, CL_TRUE, 0, sizeof(uint64_t) * global_work_size, saved_idx, 0, NULL, NULL), "failed in clEnqueueWriteBuffer buffer_idx");

	// Execute method
	clEnqueueNDRangeKernel( queue[ocl_gpu_id], crypt_kernel, 1, NULL, &global_work_size, &local_work_size, 0, NULL, profilingEvent);
	clFinish( queue[ocl_gpu_id] );

	// Read partial result
	clEnqueueReadBuffer(queue[ocl_gpu_id], buffer_out, CL_TRUE, 0, sizeof(cl_uint)*global_work_size, bbbs, 0, NULL, NULL);

	max_key_length = 0;
	have_full_hashes = 0;

	return count;
}

static int crypt_all(int *pcount, struct db_salt *salt)
{
	unsigned int i;

	if(mask_mode)
		*pcount *= multiplier;

	if(loaded_count != (salt->count)) {
		load_hash(salt);
		load_bitmap(loaded_count, 0, &bitmap1[0].bitmap0[0], (BITMAP_SIZE_1 / 8));
		load_bitmap(loaded_count, 1, &bitmap1[0].bitmap1[0], (BITMAP_SIZE_1 / 8));
		load_bitmap(loaded_count, 2, &bitmap1[0].bitmap2[0], (BITMAP_SIZE_1 / 8));
		load_bitmap(loaded_count, 3, &bitmap1[0].bitmap3[0], (BITMAP_SIZE_1 / 8));
		load_bitmap(loaded_count, 0, &bitmap1[0].gbitmap0[0], (BITMAP_SIZE_3 / 8));
		load_hashtable_plus(&bitmap2[0].hashtable0[0], &bitmap1[0].loaded_next_hash[0], 2, loaded_count, HASH_TABLE_SIZE_0);
		HANDLE_CLERROR(clEnqueueWriteBuffer(queue[ocl_gpu_id], buffer_bitmap1, CL_TRUE, 0, sizeof(struct bitmap_context_mixed), bitmap1, 0, NULL, NULL), "Failed Copy data to gpu");
		HANDLE_CLERROR(clEnqueueWriteBuffer(queue[ocl_gpu_id], buffer_bitmap2, CL_TRUE, 0, sizeof(struct bitmap_context_global), bitmap2, 0, NULL, NULL), "Failed Copy data to gpu");
	}

	// copy keys to the device
	HANDLE_CLERROR(clEnqueueWriteBuffer(queue[ocl_gpu_id], buffer_keys, CL_TRUE, 0, 4 * key_idx, saved_plain, 0, NULL, NULL), "failed in clEnqueueWriteBuffer buffer_keys");
	HANDLE_CLERROR(clEnqueueWriteBuffer(queue[ocl_gpu_id], buffer_idx, CL_TRUE, 0, sizeof(uint64_t) * global_work_size, saved_idx, 0, NULL, NULL), "failed in clEnqueueWriteBuffer buffer_idx");

	if(msk_ctx.flg_wrd)
		HANDLE_CLERROR(clEnqueueWriteBuffer(queue[ocl_gpu_id], buffer_outKeyIdx, CL_TRUE, 0,
			(DB->format->params.max_keys_per_crypt), mask_offset_buffer, 0, NULL, NULL),
			"failed in clEnqueWriteBuffer buffer_outKeyIdx");
	else {
		HANDLE_CLERROR(clSetKernelArg(zero, 1, sizeof(cl_uint), &loaded_count), "Error setting argument 1");
		HANDLE_CLERROR(clEnqueueNDRangeKernel(queue[ocl_gpu_id], zero, 1, NULL, &global_work_size, &local_work_size, 0, NULL, NULL), "failed in clEnqueueNDRangeKernel zero");
		clFinish(queue[ocl_gpu_id]);
	}

	// Execute method
	clEnqueueNDRangeKernel( queue[ocl_gpu_id], crk_kernel, 1, NULL, &global_work_size, &local_work_size, 0, NULL, profilingEvent);
	clFinish( queue[ocl_gpu_id] );

	cmp_out = 0;
	// read back compare results
	HANDLE_CLERROR(clEnqueueReadBuffer(queue[ocl_gpu_id], buffer_outKeyIdx, CL_TRUE, 0, sizeof(cl_uint) * loaded_count, outKeyIdx, 0, NULL, NULL), "failed in reading cracked key indices back");

	// If a positive match is found outKeyIdx[i] contains some positive vlaue else contains 0
	for(i = 0; i < (loaded_count & (~cmp_out)); i++)
		cmp_out = outKeyIdx[i]?0xffffffff:0;

	if(cmp_out) {
		HANDLE_CLERROR(clEnqueueReadBuffer(queue[ocl_gpu_id], buffer_outKeyIdx, CL_TRUE, 0, sizeof(cl_uint) * loaded_count * 2, outKeyIdx, 0, NULL, NULL), "failed in reading cracked key indices back");
		for(i = 0; i < loaded_count; i++) {
			if(outKeyIdx[i])
				bbbs[i] = loaded_hashes[i+ loaded_count + 1];
			else bbbs[i] = 0;
		}
		if(msk_ctx.flg_wrd)
			memcpy(mask_offsets, mask_offset_buffer, (DB->format->params.max_keys_per_crypt));
		have_full_hashes = 0;
		return loaded_count;
	}

	else return 0;
}

static void clear_keys() {

	num_keys = 0;
	key_idx = 0;
}

struct fmt_main fmt_opencl_NT = {
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
		DEFAULT_ALIGN,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
		(26*26*10),
		FMT_CASE | FMT_8_BIT | FMT_SPLIT_UNIFIES_CASE | FMT_UNICODE,
		tests
	}, {
		init,
		done,
		opencl_nt_reset,
		prepare,
		valid,
		split,
		get_binary,
		fmt_default_salt,
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
		fmt_default_salt_hash,
		fmt_default_set_salt,
		set_key,
		get_key_self_test,
		clear_keys,
		crypt_all_self_test,
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
