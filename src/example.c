#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "lz4.h"
#include "zapi.h"

int main() {
	FILE* fp;
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

	// clear input to test if decompress works
	memset(in, 0, 256*8);

	printf("\nCleared buffer: '%s'\n", in);

	// do update
	delta_failed = update_block(some_data, out, 4, &p_opts, scratch);

	// check if new page was generated
	if(delta_failed) {
		size = generate_page(scratch, out, &p_opts, 256*16);
		//memcpy(out, scratch, size); // copy into some other buffer
	}

	// now decompress and check to see if data looks good
	decompress_page(out, in, &p_opts, p_opts.blocks);
	printf("\nUncomp  buffer: '%s'\n", in);

	free(scratch);

	// note I didn't call free_page because the page is on the stack here
	// but it should be called to clear the linked list which doesn't
	// exist yet
	fclose(fp);
	return 0;
}
