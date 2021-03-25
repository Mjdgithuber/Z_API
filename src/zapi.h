#ifndef __ZAPI__H__
#define __ZAPI__H__

typedef unsigned char BYTE;

struct delta;

typedef struct delta {
	struct delta* next;
	short int id;
} delta_block;

typedef struct {
	short int t_size; /* total size of compressed page + deltas in bytes */
	BYTE* delta_head; /* head of delta linked list */
} header;

typedef struct {
	short int block_sz; /* in bytes */
	short int blocks;   /* number of blocks */
	
} page_opts;

/*
 * Compresses 'src' buffer based on 'block_size' and 'blocks' and store in 'dest'
 * The # of bytes compressed from 'src' = 'block_size' * 'blocks'
 * 'thres_size' is at least the size of 'dest'
 * NOTE: dest must be allocated
 * @return # of bytes written to 'dest', returns 0 if compression failed to compress
 * 	into 'thres' bytes
 */
unsigned generate_page(BYTE* src, BYTE* dest, page_opts* p_opts, unsigned thres);

/*
 * Decompress page 'src' into 'dest'
 * 'dest' must be at least p_opts->block_sz * blocks in size
 * @return status { 0 -> failed, 1 -> success }
 */
//int decompress_page(BYTE* src, BYTE* dest, page_opts* p_opts);
int decompress_page(BYTE* src, BYTE* dest, page_opts* p_opts, unsigned blocks);


/* @return the total size of the page including metadata */
int page_size(BYTE* page);

/* frees page and associated delta blocks */
void free_page(BYTE* page);


/* @return the minimum size of scratch buffer needed to be send to update_block */
unsigned minimum_scratch_size(page_opts* p_opts);


/* Updates one block in a page, this function will do one of the following operations
 * 1) perform a delta encoding of the changed data, this will change the total size of
 *    the page but will append to an internal linked list so no data from page buffer
 *    will move.  0 will be returned if this occured
 * 2) if delta encoding doesn't meet the threshold, this function will return a new 
 *    compressed page with the new block contained within it.  This new page will be 
 *    contained in the scratch buffer.  This will return the size of the new compressed
 *    page stored in the scratch buffer
 *
 * 'src' the new block with location 'unit' to be written to 'page' 
 * 'scratch' a scratch buffer to be allocated by caller for an internal working space
 *           also to be used to return a new compressed page when needed
 *           NOTE: scratch buffer must be at least as large as minimum_scratch_size() 
 */
unsigned update_block(BYTE* src, BYTE* page, unsigned unit, page_opts* p_opts, BYTE* scratch);

#endif
