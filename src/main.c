#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lz4.h"

typedef unsigned char BYTE;



typedef struct {
	short int unit_size; /* in bytes */
	short int units;     /* number of units */
} header;

void compress_block(BYTE* src, BYTE* dest, unsigned dest_size, header* h, unsigned* size) {
	unsigned i;

	LZ4_stream_t* const lz4_s = LZ4_createStream();
	BYTE *c_src = src;   /* current data source */
	BYTE *c_dest = dest; /* current compressed destination */
	for(i = 0; i < h->units; i++) {
		const int c_bytes = LZ4_compress_fast_continue(lz4_s, c_src, c_dest, h->unit_size, dest_size - (c_dest - dest), 1);
		c_src += h->unit_size;
		c_dest += c_bytes;
	}

	(*size) = c_dest - dest;
	LZ4_freeStream(lz4_s);
}

void decompress_block(BYTE* src, BYTE* dest) {
	unsigned i, result;

	header* h = (header*) src;
	LZ4_streamDecode_t* const lz4_sd = LZ4_createStreamDecode();
	BYTE *c_src = src + sizeof(header);
	BYTE *c_dest = dest;
	for(i = 0; i < h->units; i++) {
		printf("Decompressing unit %u\n", i);
		/*result = LZ4_decompress_safe_continue(lz4_sd, c_src, c_dest, 1000, h->unit_size);*/
		result = LZ4_decompress_safe_continue_unkown_size (lz4_sd, c_src, c_dest, h->unit_size, 512);
		printf("Decompressed unit %u with compressed size of result %u\n", i, result);
		break;
	}

	LZ4_freeStreamDecode(lz4_sd);
}

void generate_block(BYTE* src, short int unit_size, short int units, BYTE* dest, unsigned dest_size, unsigned* size) {
	
	header* h = (header*) dest;
	h->unit_size = unit_size;
	h->units = units;

	compress_block(src, dest + sizeof(header), dest_size - sizeof(header), h, size);
	(*size) += sizeof(header);
}

int main() {
	FILE* fp;
	BYTE in[256];
	BYTE out[256*2];	

	fp = fopen("test_compress.txt", "r");
	
	int num = fread(in, 1, 256, fp);
	printf("Read %d chars from input!\n", num);
	
	int size;
	generate_block(in, 64, 4, out, 256*2, &size);

	printf("Output compressed size %d!\n", size);

	printf("\nTesting output block:\n");
	printf(" > Header:\n");
	printf("   > Unit Size: %u\n", ((header*)out)->unit_size);
	printf("   > Units    : %u\n", ((header*)out)->units);

	printf("\nOrginal buffer: '%s'\n", in);
	
	printf("\nCleared buffer: ");
	memset(in, 0, 256);
	printf("'%s'\n", in);	

	decompress_block(out, in);
	printf("\nUncomp  buffer: '%s'\n", in);

	fclose(fp);
	return 0;
}
