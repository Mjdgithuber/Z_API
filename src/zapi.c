#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "lz4.h"
#include "zapi.h"

#define DEBUG_FLAG 1
#define DEBUG MG "DEBUG: "
#define INFO CY "INFO: "
#define debug_printf(level, fmt, ...) \
	do { if(DEBUG_FLAG) printf(level NM fmt, ##__VA_ARGS__); } while(0)


// Internal buffer to store delta before allocation
#define DELTA_BUF_SIZE 3000

int zapi_page_size(BYTE* page) {
	return ((zapi_page_header*) page)->t_size;
}

void zapi_free_page(BYTE* page) {
	zapi_delta_block* db = (zapi_delta_block*)((zapi_page_header*) page)->delta_head;
	zapi_delta_block* tmp;

	// free delta linked list
	while(db) {
		debug_printf(DEBUG, "Freeing delta block with id %u\n", db->id);
		tmp = db->next;
		free(db);
		db = tmp;
	}
}

static void decode_packed(BYTE* orginal, int orgi_size, BYTE* delta_enc, BYTE* out) {
	unsigned i, len, buf = 0, i_c = 1, o_c = 0, bytes_left;
	unsigned consumed_cache, c_tmp = 0;
	BYTE b, token;

	bytes_left = (unsigned) delta_enc[0];
	consumed_cache = sizeof(buf)*8;
	while(o_c < orgi_size) {
		// load bytes into cache if needed
		while(consumed_cache >= 8 && bytes_left) {
			buf |= (delta_enc[i_c++] << (consumed_cache -= 8));
			bytes_left--;
		}

		token = buf >> 30;
		if(token == 0x00) {
			out[o_c] = orginal[o_c];
			c_tmp = 2;
		} else if(token == 0x01) { //buf |= ((0x01 << 8) + b) << (cur_bit - 9);
			b = (buf >> 22) & 0xff;
			out[o_c] = orginal[o_c] ^ b;
			c_tmp = 10;
		} else if(token == 0x02) {// buf |= ((0x02 << 12) + (len << 8) + b) << (cur_bit - 13);
			len = (buf >> 26) & 0xf;
			b = (buf >> 18) & 0xff;
			for(i = 0; i <= len; i++, o_c++)
				out[o_c] = orginal[o_c] ^ b;
			c_tmp = 14;
		} else if(token == 0x03 && c_tmp < 14) { // < 14 signals end if needed  TODO c_tmp will always be 0 here?
			len = (buf >> 22) & 0xff;
			b = (buf >> 14) & 0xff;
			for(i = 0; i <= len; i++, o_c++)
				out[o_c] = orginal[o_c] ^ b;
			c_tmp = 18;
		} else return; // end mark hit return
		o_c += token <= 0x01 ? 1 : 0;
	
		buf = buf << c_tmp;
		consumed_cache += c_tmp;
	}
}

static void apply_delta(zapi_page_header* h, BYTE* data, page_opts* p_opts, unsigned start, unsigned blocks) {
	BYTE* addr;
	unsigned tmp;

	zapi_delta_block* dp = h->delta_head;
	while(dp) {
		debug_printf(DEBUG, "Applying delta block #%u to decompression!\n", dp->id);
		tmp = dp->id - start;
		if(tmp >= 0 && tmp < blocks) {
			addr = data + p_opts->block_sz * tmp;
			decode_packed(addr, p_opts->block_sz, (BYTE*)dp + sizeof(zapi_delta_block), addr);
		}
		dp = dp->next;
	}
}

static int decompress_page_internal(zapi_page_header* h, BYTE* src, BYTE* dest, page_opts* p_opts, 
	LZ4_streamDecode_t* stream, unsigned start_index, unsigned blocks, unsigned blk_ext, int* src_ext) {

	unsigned i, tmp, c_read = 0;
	for(i = 0; i < blocks; i++) {
		if(blk_ext == i) *src_ext = c_read;

		tmp = LZ4_decompress_safe_continue_unkown_size (stream, (LZ4_BYTE*)src + c_read, (LZ4_BYTE*)dest, p_opts->block_sz, 512); // TODO change 512 to block_sz
		debug_printf(DEBUG, "Decompressed %u/%u! First byte (raw): %c\n", start_index+i+1, p_opts->blocks, *dest);
		c_read += tmp;
		dest += p_opts->block_sz;
	}

	// TODO return -1 if fail
	return c_read;
}

static BYTE pack_delta(unsigned length, BYTE delta, unsigned* buf, char* cur_bit, unsigned* bytes_used, BYTE* out, int max_output) {
	// a single null byte
	if(delta == 0 && length < 7) { // length 0 is 1 (because you can't have 0 length rep)
		// or with 0 same as doing nothing
		debug_printf(INFO, "Writing token 0 %u times\n", length);
		(*cur_bit) -= (length+1) * 2;
	} else if(length == 0) { // non null byte with length of 1
		debug_printf(INFO, "Writing token 1\n");
		(*buf) |= ((0x01 << 8) + delta) << (*cur_bit - 9);
		(*cur_bit) -= 10;
	} else if(length <= 0xf) { // non null bytes with length of 16 or less
		debug_printf(INFO, "Writing token 2 with length %u\n", length);
		(*buf) |= ((0x02 << 12) + (length << 8) + delta) << (*cur_bit - 13);
		(*cur_bit) -= 14;
	} else { // non null bytes with length greater than 16
		debug_printf(INFO, "Writing token 3 with length %u\n", length);
		(*buf) |= ((0x03 << 16) + (length << 8) + delta) << (*cur_bit - 17);
		(*cur_bit) -= 18;
	}
	// dump to output
	while((*cur_bit) <= 23) {
		if((*bytes_used) >= max_output)
			return 0;

		// TODO add endian check
		out[(*bytes_used)++] = ((BYTE*)buf)[3]; //TODO why is this a char MATTHEW?
		(*buf) = (*buf) << 8;
		(*cur_bit) += 8;
	}

	return 1;
}

static unsigned delta_packed(BYTE* b0, BYTE* b1, int ib_size, BYTE* b_out, int max_output_size) {

	unsigned buf = 0;
	char cur_bit = 31;
	unsigned bytes_used = 0;
	char success = 1;

	int i;
	unsigned int count = 0;
	BYTE last_d = 0;
	BYTE delta;
	int changed = 0;
	for(i = 0; i < ib_size; i++) {
		delta = b0[i] ^ b1[i];
		
		if(delta) changed++;
	
		if(delta == last_d) count++;
		if(delta != last_d || i == ib_size-1 || count == (UCHAR_MAX+1)) {

			// new 
			if(count)
				success = pack_delta(count-1, last_d, &buf, &cur_bit, &bytes_used, b_out+1, max_output_size-1);
			if(i == ib_size-1 && delta != last_d)
				success = pack_delta(0, delta, &buf, &cur_bit, &bytes_used, b_out+1, max_output_size-1);

			if(!success) return 0;

			// end of new
			//////

			count = 1;
			last_d = delta;
		}
	}

	debug_printf(DEBUG, "%u bytes changed! cur_bit: %d\n", changed, cur_bit);

	// do final dump NOTE: at this point cur_bit is guarantee to be within 7 bits of 31 so no need for a while also encode end if needed with 0x03
	if(cur_bit != 31) {
		if(bytes_used+2 > max_output_size) return 0;

		// cur bit must be between 25-29
		buf |= 0x03 << (cur_bit-25); // signal end
		b_out[(bytes_used++) + 1] = ((BYTE*)&buf)[3]; // +1 as [0] is reserved for size
	}

	// set size in bytes
	b_out[0] = bytes_used; // TODO this should most likely be 2 bytes not just one. NOTE if you change this you must also change updating total size t_size in update_delta_llist
	return changed ? bytes_used + 1 : -1;
}

static void update_delta_llist(zapi_page_header* h, BYTE* src, unsigned size, unsigned id) {
	zapi_delta_block* db = malloc(sizeof(zapi_delta_block) + size);

	// load delta block
	memcpy((BYTE*)db + sizeof(zapi_delta_block), src, size);
	db->id = id;

	// update/append to list
	zapi_delta_block** next = &(h->delta_head);
	while(*next && (*next)->id != id)
		next = &((*next)->next);
	db->next = (*next) ? (*next)->next : NULL;
	
	// update total size and free old delta if applicable
	h->t_size += size - ((*next) ? *(((BYTE*)*next)+sizeof(zapi_delta_block)) + 1 : sizeof(zapi_delta_block) * -1);
	debug_printf(DEBUG, "%s %u\n", (*next) ? "Freed older delta block and generated new delta block for id " : "generated new delta block for id ", id);
	free(*next);
	(*next) = db;
}

static unsigned compress_page_internal(LZ4_stream_t* stream, BYTE* src, BYTE* dest, unsigned thres, zapi_page_header* h, page_opts* p_opts) {
	unsigned i;

	LZ4_stream_t* const lz4_s = stream ? stream : LZ4_createStream();

	BYTE *c_src = src;   /* current data source */
	BYTE *c_dest = dest; /* current compressed destination */
	for(i = 0; i < p_opts->blocks; i++) {
		const int c_bytes = LZ4_compress_fast_continue(lz4_s, (LZ4_BYTE*)c_src, (LZ4_BYTE*)c_dest, p_opts->block_sz, thres - (c_dest - dest), 1);

		// can't compress into thres size
		if(c_bytes == 0) {
			debug_printf(DEBUG, "Failed to compress page into %u bytes!\n", thres);
			return 0;
		}

		debug_printf(DEBUG, "Compressed unit %u/%u from %u -> %d! First byte (raw): %c\n", i+1, p_opts->blocks, p_opts->block_sz, c_bytes, *c_src);
		c_src += p_opts->block_sz;
		c_dest += c_bytes;
	}
	LZ4_freeStream(lz4_s);
	return (h->t_size += (c_dest - dest));
}

static void move_deltas(BYTE* old_page, BYTE* new_page, unsigned free_blk_indx) {
	zapi_page_header* h = (zapi_page_header*) old_page;
	zapi_page_header* new_h = (zapi_page_header*) new_page;

	// free delta blocks w/ id >= free_blk_indx
	zapi_delta_block** next = &(h->delta_head), *tmp;

	while(*next) {
		if((*next)->id >= free_blk_indx) {
			tmp = *next;
			(*next) = (*next)->next;
			free(tmp); debug_printf(INFO, "Freed delta block %u\n", tmp->id);
		} else {
			new_h->t_size += *(((BYTE*)*next)+sizeof(zapi_delta_block)) + 1 + sizeof(zapi_delta_block);
			next = &((*next)->next);
		}
	}

	// move delta to new page
	new_h->delta_head = h->delta_head;
	h->delta_head = NULL;
}

static unsigned partial_recompression(page_opts* p_opts, unsigned prc_after_blk, BYTE* data, BYTE* prc_page, BYTE* old_page, unsigned cur_comp_sz, unsigned thres) {
	unsigned new_size;

	// prepare for recompression
	LZ4_stream_t* lz4_s = LZ4_createStream();
	LZ4_loadDict(lz4_s, (LZ4_BYTE*)data, (p_opts->block_sz * prc_after_blk));

	// setup new page
	zapi_page_header* new_h = (zapi_page_header*) prc_page;
	new_h->t_size = sizeof(zapi_page_header) + cur_comp_sz;
	memcpy(prc_page + sizeof(zapi_page_header), old_page + sizeof(zapi_page_header), cur_comp_sz);

	// perform compression
	page_opts n_opts = { .block_sz = p_opts->block_sz, .blocks = p_opts->blocks - prc_after_blk };
	new_size = compress_page_internal(lz4_s, data + p_opts->block_sz * prc_after_blk, prc_page + sizeof(zapi_page_header) + cur_comp_sz, thres - sizeof(zapi_page_header) - cur_comp_sz, new_h, &n_opts);

	// move old delta to new page if needed
	if(new_size > 0)
		move_deltas(old_page, prc_page, prc_after_blk);
	debug_printf(INFO, "Attempted PRC on block %u onwards, PRC Page size = %u, FULL size = %u!\n", prc_after_blk, new_size, zapi_page_size(prc_page));

	return new_size;
}

unsigned zapi_delete_block(BYTE* page, page_opts* p_opts, BYTE* new_page, BYTE* scratch, unsigned start_block, unsigned del_blocks, unsigned thres) {
	int sr_pre_block, i;

	// decompress page
	zapi_page_header* h = (zapi_page_header*) page;
	LZ4_streamDecode_t* const lz4_sd = LZ4_createStreamDecode();
	decompress_page_internal(h, page + sizeof(zapi_page_header), scratch, p_opts, lz4_sd, 0, p_opts->blocks, start_block, &sr_pre_block); // TODO don't need to decompress if connected to last block
	apply_delta(h, scratch, p_opts, 0, p_opts->blocks);
	LZ4_freeStreamDecode(lz4_sd);

	// delete the data
	memset(scratch + start_block * p_opts->block_sz, 0, del_blocks * p_opts->block_sz);

	// prc
	return partial_recompression(p_opts, start_block, scratch, new_page, page, sr_pre_block, thres);
}

// 0 - delta enc worked, 1 - no change was made, 2 - prc succeeded, 3 - decompressed data in scratch is valid
unsigned zapi_update_block(BYTE* src, BYTE* page, unsigned block, page_opts* p_opts, BYTE* scratch, unsigned delta_thres, BYTE disable_prc, unsigned comp_thres, BYTE* prc_page, unsigned* prc_size) {
	// stack based allocation for delta probing
	BYTE delta_buf[DELTA_BUF_SIZE];
	int src_read, sr_pre_block, d_size = -1;

	// decompress up to block to be updated
	zapi_page_header* h = (zapi_page_header*) page;
	LZ4_streamDecode_t* const lz4_sd = LZ4_createStreamDecode();
	src_read = decompress_page_internal(h, page + sizeof(zapi_page_header), scratch, p_opts, lz4_sd, 0, block + 1, block, &sr_pre_block);

	// check if delta should be performed
	if(block < p_opts->prc_thres || disable_prc) {
		debug_printf(INFO, "Attempting delta on block %u!\n");
		d_size = delta_packed(src, scratch + p_opts->block_sz * block, p_opts->block_sz, (BYTE*) &delta_buf, delta_thres);
		if(d_size > 0)
			update_delta_llist(h, (BYTE*) &delta_buf, d_size, block);
		else
			debug_printf(DEBUG, "%s", (d_size == -1 ? "Nothing to change!" : "Delta failed!\n"));

		// no need to perform further decompression
		if(d_size != 0) {
			debug_printf(DEBUG, "Delta size: %d + %zu (in overhead)\n", d_size, sizeof(zapi_delta_block));
			LZ4_freeStreamDecode(lz4_sd);
			return d_size == -1;
		}
	}

	// decompress remaining page
	decompress_page_internal(h, page + sizeof(zapi_page_header) + src_read, scratch + p_opts->block_sz * (block + 1), p_opts, lz4_sd, block+1, p_opts->blocks - (block+1), -1, NULL);
	LZ4_freeStreamDecode(lz4_sd);

	// apply deltas at and after block and copy update into buffer
	apply_delta(h, scratch + p_opts->block_sz * block, p_opts, block, p_opts->blocks); // TODO exclude blocks to be updated
	memcpy(scratch + p_opts->block_sz * block, src, p_opts->block_sz);

	if(!disable_prc)
		*prc_size = partial_recompression(p_opts, block, scratch, prc_page, page, sr_pre_block, comp_thres);
	
	if(disable_prc || *prc_size <= 0)
		apply_delta(h, scratch, p_opts, 0, block);

	return !(!disable_prc ? *prc_size : 0) + 2;
}

int zapi_decompress_page(BYTE* src, BYTE* dest, page_opts* p_opts, unsigned blocks) {
	zapi_page_header* h = (zapi_page_header*) src;
	LZ4_streamDecode_t* const lz4_sd = LZ4_createStreamDecode();
	
	decompress_page_internal(h, src + sizeof(zapi_page_header), dest, p_opts, lz4_sd, 0, blocks, -1, NULL);
	apply_delta(h, dest, p_opts, 0, p_opts->blocks);

	LZ4_freeStreamDecode(lz4_sd);
	return 1;
}

unsigned zapi_generate_page(BYTE* src, BYTE* dest, page_opts* p_opts, unsigned thres) {
	// thres check
	if(thres < sizeof(zapi_page_header)) {
		debug_printf(DEBUG, "Threshold of %u bytes not large enough to store page header (%zu)!\n", thres, sizeof(zapi_page_header));
		return 0;
	}

	// fill in header
	zapi_page_header* h = (zapi_page_header*) dest;
	h->t_size = sizeof(zapi_page_header);
	h->delta_head = NULL;

	return compress_page_internal(NULL, src, dest + sizeof(zapi_page_header), thres - sizeof(zapi_page_header), h, p_opts);
}

int zapi_pack_page(BYTE* page, page_opts* p_opts, BYTE* scratch, BYTE* new_page, unsigned thres, BYTE force_recompression) {
	zapi_page_header* h = (zapi_page_header*) page;

	// assume no delta means its already fully compressed
	if(!h->delta_head && !force_recompression)
		return -1;

	// decompress and generate new page
	zapi_decompress_page(page, scratch, p_opts, p_opts->blocks);
	return zapi_generate_page(scratch, new_page, p_opts, thres);
}
