/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2021 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "a1fs.h"
#include "map.h"


/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help  = true; return true;// skip other arguments
			case 'f': opts->force = true; break;
			case 'z': opts->zero  = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (!opts->n_inodes) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	//TODO: check if the image already contains a valid a1fs superblock
	(void)image;
	a1fs_superblock *sb = (a1fs_superblock *)(image);
	if (sb->magic == A1FS_MAGIC){
		return true;
	}
	return false;
}


/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	//TODO: initialize the superblock and create an empty root directory
	//NOTE: the mode of the root directory inode should be set to S_IFDIR | 0777
	(void)image;
	(void)size;
	(void)opts;
	// initialize super block
	a1fs_superblock sb = {0};
	
	sb.magic = A1FS_MAGIC;
	sb.size = size;
	int inode_bit_size = opts->n_inodes / (4096*8) + 1;
	int inode_size = opts->n_inodes / 64 + 1;   // number of blocks inodes take
	sb.start_inode = inode_bit_size + 3;
	sb.start_extent = sb.start_inode + inode_size;
	sb.start_data = sb.start_extent + opts->n_inodes;
	sb.start_inode_map = 1;
	sb.start_data_map = 1+inode_bit_size;
	sb.inodes_count = opts->n_inodes;
	sb.blocks_count = size/A1FS_BLOCK_SIZE - (sb.start_extent + opts->n_inodes + 3);
	sb.free_inodes_count = opts->n_inodes;
	sb.free_blocks_count = size/A1FS_BLOCK_SIZE - (sb.start_extent + opts->n_inodes + 3);

	memcpy(image, &sb, sizeof(a1fs_superblock));

	// create empty root dir
	a1fs_inode root = {.mode =S_IFDIR | 0777, .links=2, .size=512, .ino_number = 0, .extent_count = 1};
	memcpy((image + sb.start_inode * A1FS_BLOCK_SIZE), &root, sizeof(a1fs_inode));

	bitmap bit_zero = {.map={0}};
	for(int i=0; i<inode_bit_size; i++){
		memcpy(image+sb.start_inode_map*A1FS_BLOCK_SIZE*i, &bit_zero, sizeof(bit_zero));
	}
	memcpy(image+sb.start_data_map*A1FS_BLOCK_SIZE, &bit_zero, sizeof(bit_zero));
	memcpy(image+(sb.start_data_map+1)*A1FS_BLOCK_SIZE, &bit_zero, sizeof(bit_zero));
	//test
	memcpy(image, &sb, sizeof(a1fs_superblock));

	unsigned int start = 1 << 7;
	// start = 1000 0000
	memcpy(image+sb.start_inode_map*A1FS_BLOCK_SIZE, &start, sizeof(start)); 
	//set begining inode, data bitmap to 1
	memcpy(image+sb.start_data_map*A1FS_BLOCK_SIZE, &start, sizeof(start));
	// change extent and data block
	a1fs_extent root_extent = {.start = 0, .count = 1};
	a1fs_dentry root_entry_self = {.ino = 0, .name = "."};
	a1fs_dentry root_entry_parent = {.ino = 0, .name = ".."};
	memcpy(image+sb.start_data*A1FS_BLOCK_SIZE, &root_entry_self, sizeof(a1fs_dentry));
	memcpy(image+sb.start_data*A1FS_BLOCK_SIZE+256, &root_entry_parent, sizeof(a1fs_dentry));
	memcpy(image+sb.start_extent*A1FS_BLOCK_SIZE, &root_extent, sizeof(root_extent));
	sb.free_blocks_count = sb.free_blocks_count-1;
	sb.free_inodes_count = sb.free_inodes_count-1;
	return true;
}


int main(int argc, char *argv[])
{
	mkfs_opts opts = {0};// defaults are all 0
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) {
		return 1;
	}

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image)) {
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) {
		memset(image, 0, size);
	}
	if (!mkfs(image, size, &opts)) {
		//a1fs_superblock *sb2 = (a1fs_superblock*) image;
    	//printf("\n2:   %ld", sb2->magic);
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}
	// a1fs_superblock *sb2 = (a1fs_superblock*) image;
    // printf("\n3:  %ld", sb2->magic);
	ret = 0;
end:
	munmap(image, size);
	return ret;
}
