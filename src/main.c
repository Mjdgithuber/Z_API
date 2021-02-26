#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

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

void decompress_block(BYTE* src, BYTE* dest, int units) {
	unsigned i, result;

	header* h = (header*) src;
	LZ4_streamDecode_t* const lz4_sd = LZ4_createStreamDecode();
	BYTE *c_src = src + sizeof(header);
	BYTE *c_dest = dest;
	for(i = 0; i < ((units == -1) ? h->units : units); i++) {
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

/*void inplace_edit(BYTE* block, BYTE* edit, unsigned start_unit, unsigned end_unit) {
	
	header* h = (header*) src;
	BYTE* decomp_buf = malloc(h->unit_size * h->units);

	decompress_block(block, decomp_buf, -1);
	
	// replace bytes in decomp buffer
	// load dict from [0, start_unit)
	// recompress

	free(decomp_buf);
}*/

void generate_block(BYTE* src, short int unit_size, short int units, BYTE* dest, unsigned dest_size, unsigned* size) {
	
	header* h = (header*) dest;
	h->unit_size = unit_size;
	h->units = units;

	compress_block(src, dest + sizeof(header), dest_size - sizeof(header), h, size);
	(*size) += sizeof(header);
}

void bin(BYTE n) {
	BYTE i;

	for(i = 1 << (sizeof(BYTE)*8 - 1); i > 0; i = i / 2)
		(n & i) ? printf("1") : printf("0");
}

void print_buf(BYTE* buf, int size) {
	int i;
	
	for(i = 0; i < size; i++) {
		bin(buf[i]);
		printf(" ");
	}
	printf("\n");
}

unsigned delta(BYTE* b0, BYTE* b1, int ib_size, BYTE* b_out, int max_output_size) {
	int i;
	BYTE* enc = b_out+1; // +1 for size

	int valid = 0;
	unsigned int count = 0;
	BYTE last_b = 0;
	BYTE delta;
	for(i = 0; i < ib_size; i++) {
		delta = b0[i] ^ b1[i];
		if(delta == last_b) count++;
		if(delta != last_b || i == ib_size-1 || count == (UCHAR_MAX+1)) {
			if(count) {
				// bounds check
				if(valid + 3 > max_output_size || ((valid + 2) > UCHAR_MAX)) return 0;

				enc[valid++] = count-1;
				enc[valid++] = last_b;
			}
			count = 1;
			last_b = delta;
		}
	}

	b_out[0] = valid;
	for(i = 0; i < valid; i += 2) {
		printf("%d %d -> ", enc[i], enc[i+1]);
	}
	printf("\n");
	return valid + 1;
}

int main() {
	BYTE* joemma = malloc(100);
	int i = delta("A bunch of happy emmas enj", "A bunch of happy teeas enj", 26, joemma, 100);
	printf("Ret: %d\n", i);
	free(joemma);
	/*FILE* fp;
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

	decompress_block(out, in, -1);
	printf("\nUncomp  buffer: '%s'\n", in);

	fclose(fp);*/
	return 0;
}
