#ifndef __ZAPI__H__
#define __ZAPI__H__

#define NM  "\x1B[0m"
#define RD  "\x1B[31m"
#define GN  "\x1B[32m"
#define MG  "\x1B[35m"
#define YL  "\x1B[33m"
#define BL  "\x1B[34m"
#define CY  "\x1B[36m"
#define WHB "\x1B[47m"

typedef unsigned char BYTE;
typedef char          LZ4_BYTE;

struct delta;

typedef struct __attribute__((__packed__)) delta {
	struct delta* next;
	short int id;
} zapi_delta_block;

typedef struct {
	short int t_size; /* total size of compressed page + deltas in bytes */
	zapi_delta_block* delta_head; /* head of delta linked list */
} zapi_page_header;

typedef struct {
	short int block_sz; /* in bytes */
	short int blocks;   /* number of blocks */
	short int prc_thres;
} page_opts;

/*
 * Compresses 'src' buffer based on 'p_opts' and store in 'dest'
 *
 * 'src' the raw data to be compressed
 * 'dest' an allocated buffer to store the compressed page, must be >= 'thres'
 * 'p_opts' page options
 * 'thres' the max bytes that can be written to 'dest'
 *
 * @return # of bytes written to 'dest', returns 0 if compression failed to compress into 'thres' bytes
 */
unsigned zapi_generate_page(BYTE* src, BYTE* dest, page_opts* p_opts, unsigned thres);

/*
 * Decompress page 'src' into buffer 'dest'.  'blocks' can be used to decompress
 * the page partially.
 *
 * 'src' the page to be decompressed
 * 'dest' the buffer to store the decompressed data, must be >= p_opts->block_sz * blocks
 * 'p_opts' page options
 * 'blocks' number of blocks to decompress
 *
 * @return status { 0 -> failed, 1 -> success }
 */
int zapi_decompress_page(BYTE* src, BYTE* dest, page_opts* p_opts, unsigned blocks);


/* 
 * This function will return the total size of that the page and associated
 * metadata occupies.  This can differ from the size of the buffer that holds
 * 'page' as zapi can allocate memory to be linked from the page during a block
 * update/delete operation
 *
 * 'page' the page to get the total size of
 *
 * @return the total size of the page including metadata 
 */
int zapi_page_size(BYTE* page);

/* 
 * This function will free any internal overhead associated with the page.  This
 * function will only clear memory allocated by the zapi library and not the 
 * buffer containing 'page' as this is externally allocated.  This function should
 * always be used before freeing 'page'
 *
 * 'page' the page to be freed
 */
void zapi_free_page(BYTE* page);

/* Updates one block in a page, this function will do one of the following operations
 * 0) perform a delta encoding of the changed data, this will change the total size of
 *    the page but will append to an internal linked list so no data from page buffer
 *    will move.  0 will be returned if this occureda
 * 1) during the delta encoding stage it is found that no bytes changed and therefore
 *    the page remains completely unchanged.  1 will be returned in this case
 * 2) A successfull partial recompression of the page took place and is stored in
 *    'prc_page' with size 'prc_size'.  2 will be returned in this case
 * 3) if delta encoding can't meet the 'delta_thres' and prc is either disabled or 
 *    p_opts->prc_thres can't be met then 3 will be returned.  In this case the 
 *    decompressed page with the updated data will be written to 'scratch'
 *
 * 'src' the new block with location 'block' within the page
 * 'page' the page to be updated or used when constructing a new page during partial recompression (prc)
 * 'block' the location of the block to be updated within 'page'
 * 'p_opts' page options
 * 'scratch' a decompression buffer that must be >= size of uncompressed page
 * 'delta_thres' the max number of bytes the delta encoding can occupy
 * 'disable_prc' if asserted will prevent a partial recompression
 * 'comp_thres' if a partial recompression occurs this is the max number of bytes it can occupy
 * 'prc_page' an allocated buffer to store a new page after partial recompression
 * 'prc_size' the size of the new page after partial recompression
 *
 * NOTE: both 'prc_page' & 'prc_size' can/should be NULL iff disable_prc = 1
 *
 * @ return the operation that was perfomed as indicated in the list above
 */
unsigned zapi_update_block(BYTE* src, BYTE* page, unsigned block, page_opts* p_opts, BYTE* decompression_buffer, unsigned delta_thres);

/*
 * Sets a continuous number of blocks from 'start_block' to 'start_block + del_blocks'
 * to zero bytes.  It will then recompress the page if it can be stored in 'thes' bytes.
 *
 * 'page' the compressed page containing the blocks to be cleared
 * 'p_opts' page options
 * 'new_page' an allocated buffer to store the new page in of size >= 'thres'
 * 'scratch' a decompression buffer that must be >= size of uncompressed page
 * 'start_block' the index of the first block to delete
 * 'del_blocks' the # of blocks to delete starting a 'start_block' 1 <= 'del_blocks' <= p_opts->blocks - 'start_block'
 * 'thres' the threshold to recompress the page into
 *
 * @return the size of the new page stored in 'new_page' (0 if thres isn't met)
 * 
 * NOTE: scratch will always contain the decompressed page with the specified blocks cleared
 */
void zapi_delete_block(BYTE* page, unsigned start_block, unsigned del_blocks);

/*
 * Will likely be renamed in the future.
 * This function takes in a compressed page and will recompress it if needed.  This
 * function will only recompress the page if there is existing delta blocks, however
 * it is still possible to force a recompression with the force_recompression arg.
 * 
 * 'page' the page to be recompressed if needed
 * 'p_opts' page options
 * 'scratch' a decompression buffer that must be >= size of uncompressed page
 * 'new_page' an allocated buffer to store the new page in of size >= 'thres'
 * 'thres' the threshold to recompress the page into
 * 'force_recompression' will recompress the page even if there is no delta blocks
 *
 * @return the number of bytes that new_page needs, can also return 0 (if recompression
 * fails to fit into 'thres' bytes) or -1 (if no recompression was attempted)
 *
 * Note if @return >= 0 then 'scratch' will contain the decompressed 'page'
 */
int zapi_pack_page(BYTE* page, page_opts* p_opts, BYTE* scratch, BYTE* new_page, unsigned thres, BYTE force_recompression);

/*
 * Given a compressed page 'zapi_page' and an orignal block 'original' this will
 * check if there is an existing delta encoding for with 'block_id' stored within
 * the page.  This will avoid having to decompress the page before applying the
 * delta.  If no delta is found then original will be copied to ret.
 *
 * 'zapi_page' the compressed page with the delta linked list to check against
 * 'p_opts' page options (only uses block_sz)
 * 'original' the original block to avoid decompression
 * 'ret' a return buffer to store the data with the delta applied to it
 * 'block_id' block id of the original block
 */
void zapi_apply_delta(BYTE* zapi_page, page_opts* p_opts, BYTE* original, BYTE* ret, unsigned block_id);

#endif
