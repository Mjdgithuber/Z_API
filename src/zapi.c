#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "lz4.h"
#include "zapi.h"



/*void update(BYTE* src, BYTE* block, BYTE* new_block, int dest_size, int unit) {
	header* h = (header*) block;
	BYTE* tmp = malloc(h->unit_size * h->units);

	// for now decompress the entire block
	decompress_block(block, tmp, -1);

	// overwrite data
	memcpy(tmp + (h->unit_size * unit), src, h->unit_size);

	// rebuild dict
	LZ4_loadDict(make_stream_here, tmp, (h->unit_size * unit));

	// recompress need to fix the function call with header
	compress_block(tmp, new_block, dest_size, h, TODO_size ret);
}*/


/*void inplace_edit(BYTE* block, BYTE* edit, unsigned start_unit, unsigned end_unit) {
	
	header* h = (header*) src;
	BYTE* decomp_buf = malloc(h->unit_size * h->units);

	decompress_block(block, decomp_buf, -1);
	
	// replace bytes in decomp buffer
	// load dict from [0, start_unit)
	// recompress

	free(decomp_buf);
}*/


int page_size(BYTE* page) {
	return ((header*) page)->t_size;
}

void free_page(BYTE* page) {
	delta_block* db = (delta_block*)((header*) page)->delta_head;
	delta_block* tmp;

	// free delta linked list
	while(db) {
		tmp = db->next;
		free(db);
		db = tmp;
	}

	free(page);
}

static unsigned compression_max_size(page_opts* p_opts) {
	return LZ4_COMPRESSBOUND(p_opts->block_sz) * p_opts->blocks;
}

unsigned minimum_scratch_size(page_opts* p_opts) {
	return compression_max_size(p_opts) +         // for recompression
		(p_opts->block_sz * p_opts->blocks);  // for decompression
}


unsigned update_block(BYTE* src, BYTE* page, unsigned unit, page_opts* p_opts, BYTE* scratch) {
	//if(!scratch) scratch = malloc(p_opts->blocks * p_opts->block_sz);
	
	// decompress entire page (TODO do partial and delta check)
	unsigned offset = compression_max_size(p_opts);
	decompress_page(page, scratch + offset, p_opts);

	// for now just overwrite data and recompress (no delta yet)
	memcpy(scratch + offset + (p_opts->block_sz * unit), src, p_opts->block_sz);
	
	// TODO must replace offset with something else
	return generate_page(scratch + offset, scratch, p_opts, offset);
}

/*unsigned update_block(BYTE* src, BYTE* page, unsigned unit, page_opts* p_opts, BYTE* scratch) {
	//if(!scratch) scratch = malloc(p_opts->blocks * p_opts->block_sz);
	
	// decompress entire page (TODO do partial and delta check)
	decompress_page(page, scratch, p_opts);

	header* h = (header*) page;
	delta_block** cur_delta = &((delta_block*)h->delta_head);
	delta_block*  next_delta = (*cur_delta)->next;
	while(next_delta) {
		if(next_delta->id == unit) break;

		(*cur_delta) = &next_delta;// not going to work
		next_delta = next_delta->next;
	}

	// generate new delta block
	
	// append or edit linked chain
}*/

int decompress_page(BYTE* src, BYTE* dest, page_opts* p_opts) {
	unsigned i, result;

	header* h = (header*) src;
	LZ4_streamDecode_t* const lz4_sd = LZ4_createStreamDecode();
	BYTE *c_src = src + sizeof(header);
	BYTE *c_dest = dest;
	for(i = 0; i < p_opts->blocks; i++) {
		result = LZ4_decompress_safe_continue_unkown_size (lz4_sd, c_src, c_dest, p_opts->block_sz, 512);
		c_src += result;
		c_dest += p_opts->block_sz;
		printf("Decompressed unit %u with compressed size of result %u\n", i, result);
	}

	LZ4_freeStreamDecode(lz4_sd);
	return 1;
}

unsigned compress_page(BYTE* src, BYTE* dest, unsigned thres, header* h, page_opts* p_opts) {
	unsigned i;

	LZ4_stream_t* const lz4_s = LZ4_createStream();

	BYTE *c_src = src;   /* current data source */
	BYTE *c_dest = dest; /* current compressed destination */
	for(i = 0; i < p_opts->blocks; i++) {
		const int c_bytes = LZ4_compress_fast_continue(lz4_s, c_src, c_dest, p_opts->block_sz, thres - (c_dest - dest), 1);

		// can't compress into thres size
		if(c_bytes == 0) return 0;

		c_src += p_opts->block_sz;
		c_dest += c_bytes;
		printf("Compressed unit %u/%u from %u -> %d!\n", i+1, p_opts->blocks, p_opts->block_sz, c_bytes);
	}
	LZ4_freeStream(lz4_s);
	printf("Size emma: %d %d\n", (c_dest - dest), sizeof(header));
	return (h->t_size += (c_dest - dest));
}

unsigned generate_page(BYTE* src, BYTE* dest, page_opts* p_opts, unsigned thres) {
	unsigned size;

	// thres check
	if(thres < (sizeof(header) + 1)) return 0;

	// fill in header
	header* h = (header*) dest;
	h->t_size = sizeof(header);
	h->delta_head = NULL;

	return compress_page(src, dest + sizeof(header), thres - sizeof(header), h, p_opts);
}

/*unsigned delta(BYTE* b0, BYTE* b1, int ib_size, BYTE* b_out, int max_output_size) {
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
	return valid + 1;
}*/

/*unsigned delta_packed(BYTE* b0, BYTE* b1, int ib_size, BYTE* b_out, int max_output_size, BYTE* tiny) {
	int i;
	BYTE* enc = b_out+1; // +1 for size

	int valid = 0;
	unsigned int count = 0;
	BYTE last_b = 0;
	BYTE delta;

	int changed = 0;
	for(i = 0; i < ib_size; i++) {
		delta = b0[i] ^ b1[i];
		
		if(delta) changed++;
	
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

	printf("%u bytes changed!\n", changed);

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
}*/

/*void decode_packed(BYTE* orginal, int orgi_size, BYTE* delta_enc, BYTE* out) {

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
}*/

/*void decode(BYTE* orginal, BYTE* delta_enc, BYTE* out) {

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

}*/

int main() {
	/*BYTE* orgi = "A bunch of happy emmas enjoy eating all of those pineapples, but they don't enjoy being eatenj this is going to be erased but there is more tl to 88";
	BYTE* edit = "An entire flock  emmas enjoy flying all of those pineapples, but they don't enjoy being flownj                            but there is jell gx to 33";
	BYTE* joemma = malloc(strlen(orgi));
	BYTE* packer = malloc(500);

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
	free(packer);*/
	FILE* fp;
	BYTE in[2048];
	BYTE out[256*16];	

	page_opts p_opts;
	p_opts.block_sz = 256;
	p_opts.blocks = 8;

	fp = fopen("test_compress.txt", "r");
	
	int num = fread(in, 1, 2048, fp);
	printf("Read %d chars from input!\n", num);
	
	int size;
	size = generate_page(in, out, &p_opts, 256*16);

	printf("Output compressed size %d!\n", size);

	/*printf("\nTesting output block:\n");
	printf(" > Header:\n");
	printf("   > Unit Size: %u\n", ((header*)out)->unit_size);
	printf("   > Units    : %u\n", ((header*)out)->units);*/

	printf("\nOrginal buffer: '%s'\n", in);
	
	printf("\nCleared buffer: ");
	memset(in, 0, 256);
	printf("'%s'\n", in);	

	decompress_page(out, in, &p_opts);
	printf("\nUncomp  buffer: '%s'\n", in);

	fclose(fp);
	return 0;
}
