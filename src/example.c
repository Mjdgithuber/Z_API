#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "lz4.h"
#include "zapi.h"

BYTE* generate_random_edit(BYTE* dest, page_opts* p_opts, unsigned block, unsigned bytes_to_change) {
	BYTE* b_off = dest + p_opts->block_sz * block;
	unsigned i, start;

	start = rand() % (p_opts->block_sz - bytes_to_change);
	for(i = 0; i < bytes_to_change; i++)
		b_off[start++] = rand() % 256; // 0 - 255

	// return ptr to block offset within dest
	return b_off;
}

BYTE cmp_page(BYTE* page, BYTE* raw_cmp_data, page_opts* p_opts) {
	BYTE* uc_pg, ret;

	// create decompression buffer & compare
	uc_pg = malloc(p_opts->block_sz * p_opts->blocks);
	zapi_decompress_page(page, uc_pg, p_opts, p_opts->blocks);
	ret = !memcmp(raw_cmp_data, uc_pg, p_opts->blocks * p_opts->block_sz);
	free(uc_pg);

	if(ret)
		printf(GN "=== SUCCESS: Page and raw data match! ===\n" NM);
	else
		printf(RD "=== FAILURE: Page and raw data do not match! ===\n" NM);
	return ret;
}

char read_file(const char* file, BYTE* buf, unsigned size) {
	FILE* fp;
	int read;

	fp = fopen(file, "r");
	read = fread(buf, 1, size, fp);
	
	fclose(fp);
	if(read != size) {
		printf("Only able to read %u/%u bytes from %s!\n", read, size, file);
		return 0;
	} else printf("Read %d bytes from '%s'\n", read, file);

	return 1;
}

void delta_battery(page_opts* p_opts, BYTE* start_page, BYTE* start_data) {
	unsigned blk, bytes, uc_size, delta_failed, last, i;
	BYTE* edit_full, *edit_par, *scratch;

	printf(YL "\nDelta Battery Test:\n" NM);
	uc_size = p_opts->blocks * p_opts->block_sz;
	
	edit_full = malloc(uc_size);
	scratch = malloc(uc_size);

	// copy start into edit
	memcpy(edit_full, start_data, p_opts->block_sz * p_opts->blocks);

	for(i = 0; i < 2; i++) {
		blk = rand() % p_opts->blocks;
		bytes = rand() % 12;
		edit_par = generate_random_edit(edit_full, p_opts, blk, bytes);
		last = zapi_page_size(start_page);
		printf("Changed %u bytes of block %u\n", bytes, blk);
		delta_failed = zapi_update_block(edit_par, start_page, blk, p_opts, scratch, 100);
		printf("Page size %u -> %u (%u changed)\n", last, zapi_page_size(start_page), zapi_page_size(start_page) - last);
		if(delta_failed) printf("Need to allocate a new page\n");
		cmp_page(start_page, edit_full, p_opts);
	}

	printf("\n");
	
	free(scratch);
	free(edit_full);
}

void run_tests() {
	BYTE* start_data;

	page_opts p_opts;
	p_opts.block_sz = 256;
	p_opts.blocks = 8;

	// read input start file
	unsigned size = p_opts.block_sz * p_opts.blocks;
	start_data = malloc(size);
	if(!read_file("test_compress.txt", start_data, size))
		return;

	// generate test page
	unsigned p_size;
	BYTE* scratch = malloc(size);
	printf(YL "\nInit Compression Test:\n" NM);
	p_size = zapi_generate_page(start_data, scratch, &p_opts, size);

	// copy into correct size buffer
	BYTE* page = malloc(p_size);
	memcpy(page, scratch, p_size);
	printf("Generated page with compressed size of %u, REPORTED SIZE: %u\n", p_size, zapi_page_size(page));
	printf("Compression ratio %.2f\n\n", (float)size/p_size);

	cmp_page(page, start_data, &p_opts);

	delta_battery(&p_opts, page, start_data);

	zapi_free_page(page);
	free(scratch);
	free(start_data);
	free(page);

	/*printf("Decompression Test:\nDecompressing page...\n");
	BYTE* decomp_page = malloc(size);
	zapi_decompress_page(page, decomp_page, &p_opts, p_opts.blocks);
	if(!memcmp(decomp_page, test_data, size))
		printf("Decompression successfully matched original data\n");
	else
		printf("Decompression failed to match original data\n");
	
	run_delta_test(page, test_data, &p_opts, decomp_page);

	zapi_free_page(page);
	free(page);
	free(test_data);
	free(scratch);*/
}

void run_delta_test(BYTE* page, BYTE* test_data, page_opts* p_opts, BYTE* dump_page) {
	BYTE* edit_block = malloc(p_opts->block_sz);

	// copy org data and make some changes to block 4 (0 indexed)
	printf("\nRunning delta tests with starting page size %u:\n", zapi_page_size(page));
	printf("Making edits to block 4\n");
	memcpy(edit_block, test_data + 4*p_opts->block_sz, p_opts->block_sz);
	edit_block[0] = 'E';
	edit_block[1] = 'm';
	edit_block[2] = 'm';
	edit_block[3] = 'a';
	edit_block[4] = '!';

	BYTE* compare = malloc(p_opts->block_sz * p_opts->blocks);
	memcpy(compare, test_data, p_opts->block_sz * p_opts->blocks);
	memcpy(compare+4*p_opts->block_sz, edit_block, p_opts->block_sz);

	
	BYTE* scratch = malloc(p_opts->blocks * p_opts->block_sz);
	BYTE* recomp_page = malloc(10000);
	int ss = zapi_update_block_recomp(edit_block, page, 4, p_opts, scratch, 10000, recomp_page);
	//update_block_recomp(BYTE* src, BYTE* page, unsigned block, page_opts* p_opts, BYTE* scratch, unsigned thres, BYTE* recomp_page)
	
	printf("NEW SIZE: %u\n", ss);

	BYTE* tight_page = malloc(ss);
	memcpy(tight_page, recomp_page, ss);
	zapi_decompress_page(tight_page, scratch, p_opts, p_opts->blocks);

	printf("1: %s\n2: %s\n", compare, scratch);
	if(!memcmp(scratch, compare, p_opts->blocks * p_opts->block_sz))
		printf("Decompression successfully matched original data with new size of %u\n", zapi_page_size(tight_page));	
	else
		printf("Decompression failed to match original data\n");

	free(tight_page);	
	free(recomp_page);



	/*BYTE* scratch = malloc(p_opts->blocks * p_opts->block_sz);
	edit_block[4] = test_data[4*p_opts->block_sz + 4];
	update_block(edit_block, page, 4, p_opts, scratch, 100);
	edit_block[4] = '!';

	int delta_failed = update_block(edit_block, page, 4, p_opts, scratch, 100);

	if(delta_failed) printf("Need to allocate a new page\n");

	decompress_page(page, dump_page, p_opts, p_opts->blocks);
	if(!memcmp(dump_page, compare, p_opts->blocks * p_opts->block_sz))
		printf("Decompression successfully matched original data with new size of %u\n", page_size(page));
	else
		printf("Decompression failed to match original data\n");	
	
	printf("\nUncomp  buffer: '%s'\n", dump_page);

	free(scratch);
	free(edit_block);*/
}

void test() {
	BYTE* test_data;

	page_opts p_opts;
	p_opts.block_sz = 256;
	p_opts.blocks = 8;

	unsigned size = p_opts.block_sz * p_opts.blocks;
	test_data = malloc(size);
	if(!read_file("test_compress.txt", test_data, size))
		return;

	// generate test page
	unsigned p_size;
	BYTE* scratch = malloc(size * 2);
	printf("\nCompression Test:\n");
	p_size = zapi_generate_page(test_data, scratch, &p_opts, size * 2);

	// copy into correct size buffer
	BYTE* page = malloc(p_size);
	memcpy(page, scratch, p_size);
	printf("Generated page with compressed size of %u, REPORTED SIZE: %u\n", p_size, zapi_page_size(page));
	printf("Compression ratio %.2f\n\n", (float)size/p_size);

	printf("Decompression Test:\nDecompressing page...\n");
	BYTE* decomp_page = malloc(size);
	zapi_decompress_page(page, decomp_page, &p_opts, p_opts.blocks);
	if(!memcmp(decomp_page, test_data, size))
		printf("Decompression successfully matched original data\n");
	else
		printf("Decompression failed to match original data\n");
	
	run_delta_test(page, test_data, &p_opts, decomp_page);

	zapi_free_page(page);
	free(page);
	free(test_data);
	free(scratch);
}

int main() {
	run_tests();

	return 0;
/*	FILE* fp;
	BYTE in[2048];
	BYTE out[256*16];	

	page_opts p_opts;
	p_opts.block_sz = 256;
	p_opts.blocks = 8;

	fp = fopen("test_compress.txt", "r");
	
	int num = fread(in, 1, 2048, fp);
	printf("Read %d chars from input!\n", num);
	
	int size, delta_failed;
	size = generate_page(in, out, &p_opts, 256*16);

	printf("Output compressed size %d!\n", size);

	printf("\nOrginal buffer: '%s'\n", in);
	
	printf("\nCleared buffer: ");
	memset(in, 0, 256*8);
	printf("'%s'\n", in);	

	decompress_page(out, in, &p_opts, p_opts.blocks);
	printf("\nUncomp  buffer: '%s'\n", in);

	printf("\n\n\nTesting update:\n");

	// first allocate a scratch buffer for update
	BYTE* scratch = malloc(minimum_scratch_size(&p_opts));
	BYTE* some_data = malloc(256);

	// paste old data into the data array for testing
	memcpy(some_data, in + 4*256, 256);
	
	// change some data
	some_data[0] = 'E';
	some_data[1] = 'm';
	some_data[2] = 'm';
	some_data[3] = 'a';

	printf("\nCleared buffer: '%s'\n", in);

	// do update
	delta_failed = update_block(some_data, out, 4, &p_opts, scratch);

	memcpy(some_data, in + 7*256, 256);
	some_data[100] = 'F';
	some_data[101] = 'e';
	some_data[102] = 'm';
	some_data[103] = 'e';
	some_data[254] = 'x';
	some_data[255] = 'y';
	delta_failed = update_block(some_data, out, 7, &p_opts, scratch);

	// clear input to test if decompress works
	memset(in, 0, 256*8);

	// check if new page was generated
	if(delta_failed) {
		printf("Delta failed!\n");
		size = generate_page(scratch, out, &p_opts, 256*16);
		//memcpy(out, scratch, size); // copy into some other buffer
	}

	// now decompress and check to see if data looks good
	printf("\n\nStarting final decompression!\n");
	decompress_page(out, in, &p_opts, p_opts.blocks);
	printf("\nUncomp  buffer: '%s'\n", in);

	free(scratch);

	// note I didn't call free_page because the page is on the stack here
	// but it should be called to clear the linked list which doesn't
	// exist yet
	fclose(fp);
	return 0;*/
}
