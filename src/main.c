
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
	unsigned i;

	header* h = (header*) src;
	LZ4_streamDecode_t* const lz4_sd = LZ4_createStreamDecode();
	BYTE *c_src = src + sizeof(header);
	BYTE *c_dest = dest;
	for(i = 0; i < h->units; i++) {
		LZ4_decompress_safe_continue(lz4_sd, c_src, c_dest, 1000, h->unit_size);
		
	}

	LZ4_freeStreamDecode(lz4_sd);
}

int LZ4_decompress_safe_continue (LZ4_streamDecode_t* LZ4_streamDecode, const char* source, char* dest, int compressedSize, int maxOutputSize)

void generate_block(BYTE* src, short int unit_size, short int units, BYTE* dest, unsigned dest_size, unsigned* size) {
	
	header* h = (header*) dest;
	h->unit_size = unit_size;
	h->units = units;

	compress_block(src, dest + sizeof(header), dest_size - sizeof(header), h, size);
	(*size) += sizeof(header);
}

/* return status code */
/*int modify(BYTE* existing_compressed_block, BYTE* src, unsigned offset, unsigned size, BYTE* output_buffer, unsigned* output_size) {
	
}*/
















/* this is calloc'ed */
typedef struct {
	BYTE meta;
	BYTE stored_blocks;
	BYTE* c_buff;
	BYTE* uc_buff; /* will be used later to store uncompressed maybe first 4 blocks */
} slab_header;

typedef struct {
	BYTE** mapping;
	struct slab_header* header;
	int block_size; /* size of block in bytes */
	int slab_size; /* size of slab in bytes */
	int slabs;  /* number of slabs */
	int blocks_per_slab; /* number of blocks in the slab based on block_size and slab_size */
} partition;

BYTE* get_decompression_buffer(partition* par) {
	return malloc(par->block_size * par->slab_size);
}

void decompress(BYTE* compressed_buffer, ) {

}

void store_block(int lba, partition* par, void* block) {
	int slab = lba / par->blocks_per_slab;
	int offset = lba % par->blocks_per_slab;

	slab_header* header = &par->header[slab];
	BYTE* buf = get_decompression_buffer(par);	
	if(stored_blocks) {
		decompress(header->c_buff, buf, par->blocks_per_slab);
	}
	
	// clear (zero out) garbage between last block and current block
	if(stored_blocks < offset)
		memset(buf + (stored_blocks-1 * par->block_size), 0, (offset - stored_blocks) * par->block_size);
	memcpy(buf + (par->block_size * offset), block, par->block_size); // copy block into buf
	
	// re-compress buffer
	header->stored_blocks = offset+1;


}

void get_block(int lba, partition* par, void* block_store) {
	int slab = lba / par->blocks_per_slab;

	if(par->header[slab].valid) {
		// check if compressed
		// if so uncompress
		// also check if block exists
	}

	// else return all zeros
}

partition* make_partition(int block_size, int slab_size, int slabs) {
	partition* par;

	assert(!(slab_size % block_size));

	par = malloc(sizeof(partition));
	par->block_size = block_size;
	par->slab_size = slab_size;
	par->slabs = slabs;
	par->blocks_per_slab = slab_size / block_size;
	
	par->header = calloc(slabs, sizeof(slab_header));
	par->mapping = malloc(sizeof(byte*) * slabs);
	
	return par;
}

void free_partition(partition* par) {
	int i;

	for(i = 0; i < par->slabs; i++) {
		if(par->header[i].valid)
			free(&par->mapping[i]);
	}
	free(par->header);
	free(par->mapping);
	free(par);
}

