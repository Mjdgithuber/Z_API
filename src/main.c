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
			// write last run
			if(count) {
				// bounds check
				if(valid + 3 > max_output_size || ((valid + 2) > UCHAR_MAX)) return 0;

				enc[valid++] = count-1;
				enc[valid++] = last_b;
			}

			// TODO rewrite this with a loop
			if(i == ib_size-1 && delta != last_b) {
				// bounds check
				if(valid + 3 > max_output_size || ((valid + 2) > UCHAR_MAX)) return 0;
				enc[valid++] = 0;
				enc[valid++] = delta;
			}
			count = 1;
			last_b = delta;
		}
	}

	b_out[0] = valid;
	/*for(i = 0; i < valid; i += 2) {
		printf("%d %d -> ", enc[i], enc[i+1]);
	}
	printf("\n");*/
	return valid + 1;
}

unsigned delta_packed(BYTE* b0, BYTE* b1, int ib_size, BYTE* b_out, int max_output_size, BYTE* tiny) {
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
			// write last run
			if(count) {
				// bounds check
				if(valid + 3 > max_output_size || ((valid + 2) > UCHAR_MAX)) return 0;

				enc[valid++] = count-1;
				enc[valid++] = last_b;
			}

			// TODO rewrite this with a loop
			if(i == ib_size-1 && delta != last_b) {
				// bounds check
				if(valid + 3 > max_output_size || ((valid + 2) > UCHAR_MAX)) return 0;
				enc[valid++] = 0;
				enc[valid++] = delta;
			}
			count = 1;
			last_b = delta;
		}
	}

	b_out[0] = valid;

	int len;
	BYTE b;
	unsigned buf = 0;
	char cur_bit = 31;
	count = 1;
	for(i = 0; i < valid; i += 2) {
		len = enc[i];
		b = enc[i+1];
		
		// a single null byte
		if(b == 0 && len < 7) { // len 0 is 1 (because you can't have 0 length rep)
			// or with 0 same as doing nothing
			printf("Writing token 0 %u times\n", len);
			cur_bit -= (len+1) * 2;
		} else if(len == 0) { // non null byte with len of 1
			printf("Writing token 1\n");
			buf |= ((0x01 << 8) + b) << (cur_bit - 9);
			cur_bit -= 10;
		} else if(len <= 0xf) { // non null bytes with len of 16 or less
			printf("Writing token 2 with len %u\n", len);
			buf |= ((0x02 << 12) + (len << 8) + b) << (cur_bit - 13);
			cur_bit -= 14;
		} else { // non null bytes with len greater than 16
			printf("Writing token 3\n");
			buf |= ((0x03 << 16) + (len << 8) + b) << (cur_bit - 17);
			cur_bit -= 18;
		}

		// dump to output
		while(cur_bit <= 23) {
			// TODO add endian check
			tiny[count++] = ((BYTE*)&buf)[3];
			//printf("Dumped %u to tiny!\n", tiny[count-1]);
			buf = buf << 8;
			cur_bit += 8;
		}
	}
	
	// do final dump NOTE: at this point cur_bit is guarantee to be within 7 bits of 31 so no need for a while also encode end if needed with 0x03
	if(cur_bit != 31) {
		// cur bit must be between 25-29
		buf |= 0x03 << (cur_bit-25); // signal end
		tiny[count++] = ((BYTE*)&buf)[3];
	}

	// set size in bytes
	tiny[0] = count-1;
	//printf("Packed Size: %u\n", tiny[0]);

	return valid + 1;
}

void decode_packed(BYTE* orginal, int orgi_size, BYTE* delta_enc, BYTE* out) {

	// get # of seqs in delta
	unsigned seqs = (unsigned) delta_enc[0];
	unsigned i, j;

	unsigned len;
	BYTE b;

	unsigned buf = 0;
	char cur_bit = 31;
	BYTE token;
	unsigned count = 1;
	
	int bytes_left = (int) delta_enc[0];
	BYTE bytes_in_buf = 0;

	int out_count = 0;

	int consumed = 0;
	
	// init load of buffer
	while(bytes_in_buf != 4 && bytes_left) {
		buf = (buf << 8) + delta_enc[count++];
		bytes_in_buf++;
		bytes_left--;
	}
	//printf("\n\nStarting Buf %u\n", buf);

	int consumed_cache = 0;

	while(out_count < orgi_size) {
		token = buf >> 30;
		//printf("Count: %u, Token %u\n", out_count, token);
		if(token == 0x00) {
			out[out_count] = orginal[out_count];
			out_count++;
			consumed += 2;
		} else if(token == 0x01) { //buf |= ((0x01 << 8) + b) << (cur_bit - 9);
			b = (buf >> 22) & 0xff;
			out[out_count] = orginal[out_count] ^ b;
			out_count++;
			consumed += 10;
		} else if(token == 0x02) {// buf |= ((0x02 << 12) + (len << 8) + b) << (cur_bit - 13);
			len = (buf >> 26) & 0xf;
			b = (buf >> 18) & 0xff;
			for(j = 0; j <= len; j++, out_count++)
				out[out_count] = orginal[out_count] ^ b;

			consumed += 14;
		} else if(token == 0x03 && consumed < 14) { // < 14 signals end if needed
			len = (buf >> 22) & 0xff;
			b = (buf >> 14) & 0xff;
			for(j = 0; j <= len; j++, out_count++)
				out[out_count] = orginal[out_count] ^ b;
			consumed += 18;
		} else return; // end mark hit return
	
	
		buf = buf << consumed;
		consumed_cache += consumed;
		consumed = 0;
		//printf("Buf befor = %u\n", buf);
		while(consumed_cache >= 8 && bytes_left) {
			buf |= (delta_enc[count++] << (consumed_cache-8));
			consumed_cache -= 8;
			bytes_left--;
		}
		//printf("Buf after = %u\n", buf);
	}
}

void decode(BYTE* orginal, BYTE* delta_enc, BYTE* out) {

	// get # of seqs in delta
	unsigned seqs = (unsigned) delta_enc[0];
	unsigned i, j;

	unsigned len, count = 0;
	BYTE b;
	for(i = 0; i < seqs; i += 2) {
		len = delta_enc[i+1];
		b = delta_enc[i+2];

		for(j = 0; j <= len; j++, count++)
			out[count] = orginal[count] ^ b;
	}

}

int main() {
	BYTE* orgi = "A bunch of happy emmas enjoy eating all of those pineapples, but they don't enjoy being eatenj this is going to be erased but there is more tl to 88";
	BYTE* edit = "An entire flock  emmas enjoy flying all of those pineapples, but they don't enjoy being flownj                            but there is jell gx to 33";
	BYTE* joemma = malloc(strlen(orgi));
	BYTE* packer = malloc(500);

	/*int total = 0;
	int i;//000
	for(i = 0; i < 10000000; i++) total += delta_packed(orgi, edit, strlen(orgi), joemma, strlen(orgi), packer);
		//total += delta(orgi, edit, strlen(orgi), joemma, strlen(orgi));
	printf("Total %d\n", total);
	return 0;*/

	//int i = delta(orgi, edit, strlen(orgi), joemma, strlen(orgi));
	int i = delta_packed(orgi, edit, strlen(orgi), joemma, strlen(orgi), packer);
	printf("Ret: %d\nPacked size in bytes %u\n", i, packer[0]);

	BYTE* out = malloc(strlen(orgi));
	//decode(orgi, joemma, out);
	decode_packed(orgi, strlen(orgi), packer, out);
	printf("Orgi:    %s  Size: %u\n", orgi, strlen(orgi));
	printf("Decoded: %s  Size: %u\n", out, i);
	printf("Edit:    %s\n", edit);

	free(out);
	free(joemma);
	free(packer);
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
