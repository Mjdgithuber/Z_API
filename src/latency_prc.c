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

char write_file(BYTE* data, page_opts* p_opts, float cr) {
	FILE* fp;
	unsigned size = p_opts->blocks * p_opts->block_sz;
	int cast;
	char str[1000];

	// generate filename
	cast = (int) cr;
	sprintf(str, "data/cr%d-%d.raw", cast, cast+1);
	
	fp = fopen(str, "w");
	fwrite(data, size, 1, fp);

	fclose(fp);
}
char print_compression(const char* file, page_opts* p_opts, unsigned max_compress) {
	FILE* fp;
	unsigned i = 0, size = p_opts->blocks * p_opts->block_sz, p_sz;
	int read = size;
	float cr, cur_cr = 0.f;
	BYTE *buf, *page;

	buf = malloc(size);
	page = malloc(size*2);
	fp = fopen(file, "r");
	while(read == size) {
		memset(buf, 0, size);
		read = fread(buf, 1, size, fp);
		
		if(!read) break;

		p_sz = zapi_generate_page(buf, page, p_opts, size*2);
		cr = (float)size/p_sz;
		printf("Block %u has compression ratio %f\n", i++, cr);
		zapi_free_page(page);

		if(cr > cur_cr && cr < cur_cr + 1.f) {
			write_file(buf, p_opts, cr);
			++cur_cr;
		}

		if(cur_cr >= max_compress)
			break;
	}
	free(page);
	free(buf);
	fclose(fp);
}

void latency_compression(const char* file, page_opts* p_opts, unsigned blk, unsigned times, unsigned prc) {
	unsigned i, size = p_opts->blocks * p_opts->block_sz, p_sz;
	
	BYTE* file_data = malloc(size);
	if(!read_file(file, file_data, size))
		return;

	unsigned page_size;
	unsigned thres = size*2;
	BYTE* scratch = malloc(thres);
	BYTE* page;	

	page_size = zapi_generate_page(file_data, scratch, p_opts, thres);
	page = malloc(page_size);
	memcpy(page, scratch, page_size);
	printf(YL "Generated init page with raw size %u and compressed size %u! PRC flag: %u\n" NM, size, page_size, prc);
	printf("Running %u compressions with change on block %u\n", times, blk);

	BYTE* prc_page = malloc(thres);
	unsigned prc_size;
	generate_random_edit(file_data, p_opts, blk, 10);
	p_opts->prc_thres = 0;
	
	if(prc) {
		for(i = 0; i < times; i++) {
			zapi_update_block(file_data, page, blk, p_opts, scratch, 0, 0, thres, prc_page, &prc_size);
		}
	} else {
		for(i = 0; i < times; i++) {
			zapi_decompress_page(page, prc_page, p_opts, p_opts->blocks);
			//zapi_generate_page(file_data, scratch, p_opts, thres);
			

			//printf("New size %u!\n", zapi_generate_page(file_data, scratch, p_opts, thres));
		}
	}

	free(prc_page);
	zapi_free_page(page);
	free(page);
	free(scratch);
	free(file_data);
}

int main(int argc, char** argv) {
	int block, runs, prc;

	page_opts p_opts;
	p_opts.block_sz = 512;
	p_opts.blocks = 8;

	block = atoi(argv[2]);
	runs = atoi(argv[3]);
	prc = atoi(argv[4]);

	latency_compression(argv[1], &p_opts, block, runs, prc);


	/*page_opts p_opts;
	p_opts.block_sz = 512;
	p_opts.blocks = 8;
	print_compression("2019_oct.core", &p_opts, 12);*/


	//print_compression("data/cr4-5.raw", &p_opts, 12);
	//latency_compression("data/cr4-5.raw", &p_opts, 6, 10000000, 0);
	//latency_compression("test_compress.txt", &p_opts, 6, 10000000, 1);
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

	// deltas
	for(i = 0; i < 4; i++) {
		blk = rand() % p_opts->blocks;
		bytes = rand() % 12;
		edit_par = generate_random_edit(edit_full, p_opts, blk, bytes);
		last = zapi_page_size(start_page);
		printf("Changed %u bytes of block %u\n", bytes, blk);


		delta_failed = zapi_update_block(edit_par, start_page, blk, p_opts, scratch, 100, 1, 0, NULL, NULL);
		//delta_failed = zapi_update_block(edit_par, start_page, blk, p_opts, scratch, 100);
	
		printf("Update block returned %d!\n", delta_failed);		

		printf("Page size %u -> %u (%d changed)\n", last, zapi_page_size(start_page), zapi_page_size(start_page) - last);
		if(delta_failed) printf("Need to allocate a new page\n");
		cmp_page(start_page, edit_full, p_opts);
	}


	memset(scratch, 0, uc_size);
	BYTE* del_page = malloc(4000);
	int start = 4, total = 2;
	int del_size = zapi_delete_block(start_page, p_opts, del_page, scratch, start, total, 4000);
	printf(YL "\n\nDelete page size: %u!\n" NM, del_size);
	
	memset(edit_full + start * p_opts->block_sz, 0, total * p_opts->block_sz);
	cmp_page(del_page, edit_full, p_opts);

	zapi_free_page(del_page);
	free(del_page);



	/*BYTE* old_page = start_page;
	BYTE* new_page = malloc(3000);
	unsigned new_size;
	p_opts->prc_thres = 0;
	for(i = 0; i < 10; i++) {
		blk = rand() % p_opts->blocks;
		bytes = rand() % 12;
		edit_par = generate_random_edit(edit_full, p_opts, blk, bytes);
		last = zapi_page_size(old_page);
		printf("Changed %u bytes of block %u\n", bytes, blk);

		
		delta_failed = zapi_update_block(edit_par, old_page, blk, p_opts, scratch, 100, 0, 3000, new_page, &new_size);
		if(delta_failed != 2) {
			printf(RD "PRC FAILURE!\n" NM);
			return;
		}
		if(last != zapi_page_size(old_page)) {
			printf(RD "PRC FAILURE changed old page!\n" NM);
			return;
		}

		// copy page into tight buffer
		free(old_page);
		old_page = malloc(new_size);
		memcpy(old_page, new_page, new_size);

		//delta_failed = zapi_update_block(edit_par, start_page, blk, p_opts, scratch, 100);
	
		printf("Update block returned %d!\n", delta_failed);		

		printf("Page size %u -> %u (%d changed)\n", last, zapi_page_size(old_page), zapi_page_size(old_page) - last);
		if(delta_failed) printf("Need to allocate a new page\n");
		cmp_page(old_page, edit_full, p_opts);
	}

	// many leaks but fine for testing
	BYTE* blah_page = malloc(4000);
	printf("\n\n" YL "FINAL PRC SIZE %u, FINAL RECOMP SIZE %d! FINAL ZAPI RECOMP %d!\n" NM, 
		zapi_page_size(old_page), zapi_generate_page(edit_full, scratch, p_opts, uc_size), zapi_pack_page(old_page, p_opts, scratch, blah_page, 4000, 1));

	free(old_page);
	free(new_page);*/









	/*printf("\n\n" YL "PRC Testing:\n" NM);
	p_opts->prc_thres = 6;
	
	BYTE* new_page = malloc(4000);

	blk = 6;
	bytes = 10;
	unsigned sszz;
	edit_par = generate_random_edit(edit_full, p_opts, blk, bytes);
	last = zapi_page_size(start_page);
	delta_failed = zapi_update_block(edit_par, start_page, blk, p_opts, scratch, 100, 0, 4000, new_page, &sszz);
	printf("Update block returned %d! w/ tight size of %u\n", delta_failed, sszz);
	printf("Page size %u -> %u (%d changed)\n", last, zapi_page_size(new_page), zapi_page_size(new_page) - last);
	cmp_page(new_page, edit_full, p_opts);


	BYTE* tt_pg = malloc(sszz);
	memcpy(tt_pg, new_page, sszz);
	cmp_page(tt_pg, edit_full, p_opts);
	free(tt_pg);*/


	/*if(!memcmp(edit_full, scratch, p_opts->blocks * p_opts->block_sz))
		printf(GN "=== SUCCESS: Page and raw data match! ===\n" NM);
	else
		printf(RD "=== FAILURE: Page and raw data do not match! ===\n" NM);*/

	//free(new_page);



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
	unsigned p_size, thres = 1851;
	BYTE* scratch = malloc(thres);
	printf(YL "\nInit Compression Test with Threshold %u:\n" NM, thres);
	p_size = zapi_generate_page(start_data, scratch, &p_opts, thres);

	if(p_size) {
		// copy into correct size buffer
		BYTE* page = malloc(p_size);
		memcpy(page, scratch, p_size);
		printf("Generated page with compressed size of %u, REPORTED SIZE: %u\n", p_size, zapi_page_size(page));
		printf("Compression ratio %.2f\n\n", (float)size/p_size);

		cmp_page(page, start_data, &p_opts);

		delta_battery(&p_opts, page, start_data);

		zapi_free_page(page);
		free(page);

		memory_management_stats();
	} else {
		printf("Failed to compress page into %u bytes!\n", thres);
	}


	free(scratch);
	free(start_data);

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
	int ss = 0;//zapi_update_block_recomp(edit_block, page, 4, p_opts, scratch, 10000, recomp_page);
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

//int main() {
//	run_tests();

//	return 0;
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
//}
