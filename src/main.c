#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lz4.h"

#ifdef HC_
#include "lz4hc.h"
#endif


typedef unsigned char BYTE;

typedef struct {
	short int unit_size; /* in bytes */
	short int units;     /* number of units */
} header;

void compress_block(BYTE* src, BYTE* dest, unsigned dest_size, header* h, unsigned* size) {
	unsigned i;

#ifndef HC_
	LZ4_stream_t* const lz4_s = LZ4_createStream();
#else
	LZ4_streamHC_t lz4Stream_body = { 0 };
	LZ4_streamHC_t* lz4_s = &lz4Stream_body;
	LZ4_setCompressionLevel(lz4_s, LZ4HC_CLEVEL_MIN);
#endif

	BYTE *c_src = src;   /* current data source */
	BYTE *c_dest = dest; /* current compressed destination */
	for(i = 0; i < h->units; i++) {
#ifndef HC_
		const int c_bytes = LZ4_compress_fast_continue(lz4_s, c_src, c_dest, h->unit_size, dest_size - (c_dest - dest), 1);
#else
		const int c_bytes = LZ4_compress_HC_continue(lz4_s, c_src, c_dest, h->unit_size, dest_size - (c_dest - dest));
#endif
		c_src += h->unit_size;
		c_dest += c_bytes;
		printf("Compressed unit %u/%u from %u -> %d!\n", i+1, h->units, h->unit_size, c_bytes);
	}

	(*size) = c_dest - dest;
#ifndef HC_
	LZ4_freeStream(lz4_s);
#else
	//LZ4_freeStreamHC(lz4_s);
#endif
}

void decompress_block(BYTE* src, BYTE* dest) {
	unsigned i, result;

	header* h = (header*) src;
	LZ4_streamDecode_t* const lz4_sd = LZ4_createStreamDecode();
	BYTE *c_src = src + sizeof(header);
	BYTE *c_dest = dest;
	for(i = 0; i < h->units; i++) {
		//printf("Decompressing unit %u\n", i);
		/*result = LZ4_decompress_safe_continue(lz4_sd, c_src, c_dest, 1000, h->unit_size);*/
		result = LZ4_decompress_safe_continue_unkown_size (lz4_sd, c_src, c_dest, h->unit_size, 512);
		c_src += result;
		c_dest += h->unit_size;
		printf("Decompressed unit %u with compressed size of result %u\n", i, result);
		//break;
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
	BYTE in[2048];
	BYTE out[256*16];	

	fp = fopen("test_compress.txt", "r");
	
	int num = fread(in, 1, 2048, fp);
	printf("Read %d chars from input!\n", num);
	
	int size;
	generate_block(in, 256, 8, out, 256*16, &size);

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
