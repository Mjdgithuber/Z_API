#ifndef __ZAPI__H__
#define __ZAPI__H__

typedef unsigned char BYTE;

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
 * @return status { 0 -> failed, 1 -> success }
 */
int decompress_page(BYTE* src, BYTE* dest);

#endif
