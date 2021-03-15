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

void compress_block(BYTE* src, BYTE* dest, unsigned dest_size, header* h, unsigned* size);

void decompress_block(BYTE* src, BYTE* dest, int units);


void update(BYTE* src, BYTE* block, BYTE* new_block, int dest_size, int unit);

/*
 * Compresses 'src' buffer based on 'block_size' and 'blocks' and store in 'dest'
 * The # of bytes compressed from 'src' = 'block_size' * 'blocks'
 * 'thres_size' is at least the size of 'dest'
 * NOTE: dest must be allocated
 * @return # of bytes written to 'dest', returns 0 if compression failed to compress
 * 	into 'thres' bytes
 */
unsigned generate_page(BYTE* src, short int block_size, short int blocks, BYTE* dest, unsigned thres);
