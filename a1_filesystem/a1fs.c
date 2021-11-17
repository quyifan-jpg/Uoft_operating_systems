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
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".



/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help
	if (opts->help) {
		return true;
	}

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) {
		return false;
	}

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}

int helper(a1fs_inode *root, char *current_name){  
    //int inode_num = -1;
	fs_ctx *fs = get_fs();
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	int inodes_array[512];
	char name_array[512][252];
	int current_i = 0;
	int entry_count = root->size /256;
	a1fs_extent_block *e_block = (a1fs_extent_block *)(fs->image + (sb->start_extent+root->ino_number)* A1FS_BLOCK_SIZE);
	for (int i =0; i<root->extent_count; i++){
		int start = e_block->extent_array[i].start;
		int count =e_block->extent_array[i].count;
		for (int j = start; j< count+start; j++){
			a1fs_dentry *data_block = (a1fs_dentry*)(fs->image + (sb->start_data + j)* A1FS_BLOCK_SIZE);
			for (int x = 0; x < 16 && current_i < entry_count;x++){
				inodes_array[current_i]=data_block[x].ino;
				strcpy(name_array[current_i], data_block[x].name);
				current_i ++;
			}
		}
		//e_block->e_array[i].start;
	}
	int result = -ENOENT;
	//append all to these two array
	for(int i =0; i < current_i; i++){
		if (strcmp(name_array[i], current_name) == 0){
			result = inodes_array[i];
		}
	}
    // for loop to find right name
    return result;  
} 

/** Modify a bit to value at given index in a bitmap. */
void modify(bitmap *origin, int index, int value){
    // unsigned char bitmap[4096];
    // memcpy(bitmap, origin->map, sizeof(origin->map));
    int result = index/8;
    int remainder = index%8;
    unsigned char x1 = origin->map[result];
    unsigned char mask = 1 << (7-remainder);
    if(value==1){
        origin->map[result]=(x1|mask);
    }else{
        unsigned char mask_re = ~mask;
        origin->map[result] = (x1&mask_re);
    }
    return;
}
int find_next_bit(bitmap *origin){
    int free_inode_ind = 0;
	for (unsigned int bit_byte = 0; bit_byte < A1FS_BLOCK_SIZE; bit_byte++){
		unsigned char x1 = origin->map[bit_byte];
		printf("current inodemap: %d\n", x1);
		for(int j = 0; j < 8; j++) {
			unsigned char mask = 1 << (7-j);
			if (!(x1 & mask)) {
				return j;
			}else{
				free_inode_ind++;
			}
		}
	}
	return -1;
}
void update_extent(int ino, int index){
    fs_ctx *fs = get_fs();
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
    a1fs_inode *root = (a1fs_inode *)(fs->image + sb->start_inode * A1FS_BLOCK_SIZE + ino * 64);
    a1fs_extent_block *e_block = (a1fs_extent_block *)(fs->image + (sb->start_extent+root->ino_number)* A1FS_BLOCK_SIZE);
	int flag = 0;
    for (int i =0; i<root->extent_count; i++){
		int start = (e_block->extent_array[i]).start;
		int count = (e_block->extent_array[i]).count;
		if ((start+count) == index){
            e_block->extent_array[i].count++;
            flag=1;
        }
        if (start<=index && index <= start+count){
            fprintf(stderr, "\nupdate_extent_error\n"); 
        }
        if (flag ==1){
            break;
        }
    }
    if (flag==0){
        e_block->extent_array[root->extent_count].start = index;
        e_block->extent_array[root->extent_count].count = 1;
		root->extent_count = 1;
    }
}
int path_inode(const char *path){
	fs_ctx *fs = get_fs();
	if(path[0] != '/') {  
        fprintf(stderr, "Not an absolute path\n");  
        return -1;  
    }
	if (strcmp(path, "/") == 0) {
		return 0;
	}
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	char path_cp[4096];  
    strcpy(path_cp, path);  
    char* real_token =strtok(path_cp,"/");
    a1fs_inode *root = (a1fs_inode*)(fs->image + sb->start_inode * A1FS_BLOCK_SIZE);
	printf("sb: %d, links: %d, extent%d\n", sb->start_inode, root->links, root->extent_count);

    int result = root->extent_count;  
    while (real_token != NULL){  
		
        result = helper(root, real_token);
		if (result == -ENOENT){
			return -ENOENT;
		}
        root = (a1fs_inode *)(fs->image + sb->start_inode * A1FS_BLOCK_SIZE + result * 64);  
        real_token = strtok(NULL, "/");
		if (real_token != NULL && (root->mode|0777) != (S_IFDIR|0777)){
			return -ENOTDIR;
		}
    }
    return result;
}

/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));
	st->f_bsize   = A1FS_BLOCK_SIZE;
	st->f_frsize  = A1FS_BLOCK_SIZE;
	//TODO: fill in the rest of required fields based on the information stored
	// in the superblock
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	st->f_blocks = fs->size/A1FS_BLOCK_SIZE;
	st->f_bfree = sb->free_blocks_count;
	st->f_bavail = sb->free_blocks_count;
	st->f_files = sb->inodes_count;
	st->f_ffree = sb->free_inodes_count;
	st->f_favail = sb->free_inodes_count;
	st->f_namemax = A1FS_NAME_MAX;
	return 0;
}

void modify2(bitmap *origin, int index, unsigned char mask, int value){
    // unsigned char bitmap[4096];
    // memcpy(bitmap, origin->map, sizeof(origin->map));
    int result = index/8;
    //int remainder = index%8;
    unsigned char x1 = origin->map[result];
    if(value==1){
        origin->map[result]=(x1|mask);
    }else{
        unsigned char mask_re = ~mask;
        origin->map[result] = (x1&mask_re);
    }
    return;
}





/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the inode.
 *
 * NOTE: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= A1FS_PATH_MAX) {
		return -ENAMETOOLONG;
	}
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));

	//NOTE: This is just a placeholder that allows the file system to be mounted
	// without errors. You should remove this from your implementation.
	// if (strcmp(path, "/") == 0) {
	// 	//NOTE: all the fields set below are required and must be set according
	// 	// to the information stored in the corresponding inode
	// 	st->st_mode = S_IFDIR | 0777;
	// 	st->st_nlink = 2;
	// 	st->st_size = 0;
	// 	st->st_blocks = 0 * A1FS_BLOCK_SIZE / 512;
	// 	st->st_mtim = (struct timespec){0};
	// 	return 0;
	// }

	//TODO: lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode
	//delete
	int inode = path_inode(path);
	if (inode == -ENOENT){
		fprintf(stderr, "ENOENT-------getattr(%s, %d, %p)\n", path, inode, (void *)st);

		return -ENOENT;
	}
	if (inode == -ENOTDIR){
		return -ENOTDIR;
			fprintf(stderr, "ENOTDIR------getattr(%s, %d, %p)\n", path, inode, (void *)st);

	}
	fprintf(stderr, "--------getattr(%s, %d, %p)\n", path, inode, (void *)st);
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	a1fs_inode *test_root = (a1fs_inode*)(fs->image + sb->start_inode * A1FS_BLOCK_SIZE);
	(void)test_root;
	a1fs_inode *found = (a1fs_inode*)(fs->image + (sb->start_inode) * A1FS_BLOCK_SIZE + inode * 64);;
	a1fs_extent_block *e_block = (a1fs_extent_block*)(fs->image + (sb->start_extent+inode) * A1FS_BLOCK_SIZE);
	fprintf(stderr, "123-----getattr(%s, %d, %p)\n", path, inode, (void *)st);
	if (inode >= 0){
		st->st_ino = inode;
		st->st_mode = found->mode;
		st->st_nlink = found->links;
		st->st_size = found->size;
		int block_count=0;
		for (int i =0 ; i < found->extent_count ; i ++){
			block_count = block_count + e_block->extent_array[i].count;
		}
		st->st_blocks = block_count * A1FS_BLOCK_SIZE / 512;
		st->st_mtim = (struct timespec){0};
		return 0;
	}
		
	(void)fs;
	return -ENOSYS;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//NOTE: This is just a placeholder that allows the file system to be mounted
	// without errors. You should remove this from your implementation.
	// if (strcmp(path, "/") == 0) {
	// 	filler(buf, "." , NULL, 0);
	// 	filler(buf, "..", NULL, 0);
	// 	return 0;
	// }

	// TODO: lookup the directory inode for given path and iterate through its
	// directory entries
	int directroy_ino = path_inode(path);
	if(path[0] != '/') {  
        fprintf(stderr, "Not an absolute path\n");  
        return -1;  
    }   
	a1fs_superblock*sb = (a1fs_superblock *) fs->image;
	a1fs_extent_block* e_block = (a1fs_extent_block *) (fs->image+(sb->start_extent+ directroy_ino)*A1FS_BLOCK_SIZE);
	a1fs_inode *inode = (a1fs_inode*) (fs->image+sb->start_inode*A1FS_BLOCK_SIZE + directroy_ino * 64);
	int entry_num = inode->size / 256;
	//int inodes_array[512];
	char name_array[512][252];
	int current_i = 0;
	//int entry_count = inode->size /256;
	for (int i =0; i< inode->extent_count; i++){
		int start = e_block->extent_array[i].start;
		int count =e_block->extent_array[i].count;
		for (int j = start; j< count+start && current_i < entry_num; j++){
			a1fs_dentry *data_block = (a1fs_dentry*) (fs->image + (sb->start_data + j )* A1FS_BLOCK_SIZE);
			for (int x = 0; x < 16 && current_i < entry_num;x++){
				filler(buf, data_block[x].name , NULL, 0);
				strcpy(name_array[current_i], data_block[x].name);
				current_i ++;
			}
		}
		
	}
	fprintf(stderr, "links:%d, space:%ld\n", inode->links, inode->size);  
	//(void)inodes_array;
	(void)name_array;
	(void)fs;
	return 0;
	//return -ENOSYS;
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();
	//TODO: create a directory at given path with given mode
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	//find empty inodemj
	if (sb->free_inodes_count == 0 || sb->free_blocks_count == 0){
		return -ENOMEM;	
	}
	bitmap *inode_bm = (bitmap *)(fs->image + sb->start_inode_map*A1FS_BLOCK_SIZE);
	unsigned char ino_bm[4096];
	memcpy(ino_bm, inode_bm->map, sizeof(inode_bm->map));
	int free_inode_ind = 0;
	for (unsigned int bit_byte = 0; bit_byte < A1FS_BLOCK_SIZE; bit_byte++){
		int flag=0;
		unsigned char x1 = ino_bm[bit_byte];
		for(int j = 0; j < 8; j++) {
			unsigned char mask = 1 << (7-j);
			if (!(x1 & mask)) {
				modify(inode_bm, j, 1);
				flag=1;
				break;
			}else{
				free_inode_ind++;
			}
		}
		if (flag==1){
			break;
		}
	}
	//find empty data
	bitmap *data_bitmap = (bitmap *)(fs->image + sb->start_data_map*A1FS_BLOCK_SIZE);
	unsigned char data_bm[4096];
	memcpy(data_bm, data_bitmap->map, sizeof(data_bitmap->map));
	int free_data_ind = 0;
	for (unsigned int index = 0; index < A1FS_BLOCK_SIZE; index++){
		int flag = 0;
		unsigned char x1 = data_bm[index];
		for(int j = 0; j < 8; j++) {
			unsigned char mask = 1 << (7-j);
			if (!(x1 & mask)) {
					// update bitmap
					modify(data_bitmap, j, 1);
					flag = 1;
					break;
				}else{
					free_data_ind++;
				}
		}
		if (flag==1){
			break;
		}
	}
	printf("free inode: %d, free data:%d", free_inode_ind, free_data_ind);
	//find and update parent
    char path_cp[100];
    strcpy(path_cp, path);
	const char ch = '/';
	char *dr_name = strrchr(path_cp, ch);
	int parent_ino;
	char parent_name[A1FS_NAME_MAX];
	char *new_dr = dr_name+1;
	char parent_path[A1FS_PATH_MAX];
	strncpy(parent_path, path_cp, strlen(path_cp)-strlen(dr_name));
	char *parent_na = strrchr(parent_path, ch);
	if (parent_na == NULL){
		char parent_root[]="..";
		strcpy(parent_name, parent_root);
		parent_ino = 0;
	}else{
		strcpy(parent_name, parent_na+1);
		parent_ino = path_inode(parent_path);
	}
	a1fs_inode *parent = (a1fs_inode*) (fs->image + (sb->start_inode) * A1FS_BLOCK_SIZE + parent_ino*64);
	int extent_count = parent->extent_count;
	a1fs_extent_block *parent_block = (a1fs_extent_block *)(fs->image + (sb->start_extent+parent_ino)* A1FS_BLOCK_SIZE);
	// a1fs_dentry *parent_dr = (a1fs_dentry*) (fs->image + (sb->start_data+parent_block->extent_array[0].start)*A1FS_BLOCK_SIZE+256);
	int append_ind = parent_block->extent_array[extent_count-1].start;
	int extent_num = parent_block->extent_array[extent_count-1].count;
	int byte_more = (parent->size)%A1FS_BLOCK_SIZE;
	// if(byte_more==0){

	// }
	// create new directory
	printf("%s, %s\n", new_dr, parent_name);
	parent->size = (parent->size)+256;
	parent->links = (parent->links)+1;
	//append new directory to end of parent extent.
	// memcpy(fs->image+ (sb->start_inode+parent_ino)*A1FS_BLOCK_SIZE, &parent, sizeof(a1fs_inode));
	// a1fs_inode *parent_test = (a1fs_inode*) (fs->image + (sb->start_inode+parent_ino) * A1FS_BLOCK_SIZE);
	// printf("%d, %ld\n", parent_test->links, parent_test->size);
	a1fs_extent *new_extent = (a1fs_extent*)(fs->image+(sb->start_extent+free_inode_ind)*A1FS_BLOCK_SIZE);
	new_extent->start = free_data_ind;
	new_extent->count = 1;
	// {.start = sb->start_extent+free_data_ind, .count = 1};
	// a1fs_extent_block *new_block = (a1fs_extent_block*)(fs->image+(sb->start_extent+free_ino_ind)*A1FS_BLOCK_SIZE);

	a1fs_inode *new_ino = (a1fs_inode*)(fs->image+ (sb->start_inode) * A1FS_BLOCK_SIZE + free_inode_ind*64);
	new_ino->mode = mode;
	new_ino->links = 2;
	new_ino->size = 512;
	new_ino->ino_number = free_inode_ind;
	new_ino->extent_count = 1;
	// a1fs_inode new_ino = {.mode = mode, .links=2, .size=512, .ino_number = free_inode_ind};
	a1fs_dentry *new_entry = (a1fs_dentry*)(fs->image+(sb->start_data+free_data_ind)*A1FS_BLOCK_SIZE);
	a1fs_dentry *parent_entry = (a1fs_dentry*)(fs->image+(sb->start_data+free_data_ind)*A1FS_BLOCK_SIZE+256);
	new_entry->ino = free_inode_ind;
	strcpy(new_entry->name, ".");
	parent_entry->ino = parent_ino;
	strcpy(parent_entry->name, "..");
	a1fs_dentry *parent_append=(a1fs_dentry*)(fs->image+ (sb->start_data+append_ind+extent_num-1) * A1FS_BLOCK_SIZE + byte_more);
	parent_append->ino = free_inode_ind;
	strcpy(parent_append->name, new_dr);
	a1fs_extent_block *extent_test = (a1fs_extent_block *)(fs->image + (sb->start_extent+free_inode_ind)*A1FS_BLOCK_SIZE);
	printf("%d\n", extent_test->extent_array[0].start);
	// update sb
	sb->free_blocks_count=sb.free_blocks_count-1;
	sb->free_inodes_count=sb.free_inodes_count-1;
	return 0;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a non-root directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	assert(strcmp(path, "/") != 0);
	fs_ctx *fs = get_fs();
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	// TODO: remove the directory at given path (only if it's empty)
	int dr_ino = path_inode(path);
	a1fs_inode *parent_test = (a1fs_inode*)(fs->image + (sb->start_inode) * A1FS_BLOCK_SIZE+64);
	(void)parent_test;
	a1fs_inode *current_ino = (a1fs_inode*)(fs->image + (sb->start_inode) * A1FS_BLOCK_SIZE+dr_ino*64);
	if (current_ino->links != 2){
		return -ENOTEMPTY;
	}
	//delete extent, inode
	//update bitmaps
	bitmap *data_bitmap = (bitmap *)(fs->image + sb->start_data_map*A1FS_BLOCK_SIZE);
	bitmap *inode_bitmap = (bitmap *)(fs->image + sb->start_inode_map*A1FS_BLOCK_SIZE);
	modify(inode_bitmap, dr_ino, 0);
	sb->free_inodes_count++;
	a1fs_extent_block *rm_data_block = (a1fs_extent_block *)(fs->image + (sb->start_extent+dr_ino)*A1FS_BLOCK_SIZE);
	for (int i =0; i<current_ino->extent_count; i++){
		int start = rm_data_block->extent_array[i].start;
		int count = rm_data_block->extent_array[i].count;
		for(int m=start; m<start+count; m++){
			modify(data_bitmap, m, 0);
		}
	}
	//modify its parent
	char path_cp[100];
    strcpy(path_cp, path);
	const char ch = '/';
	char *dr_name = strrchr(path_cp, ch);
	char *new_dr = dr_name+1;
    char parent_path[A1FS_PATH_MAX];
	int parent_ino;
	
	if (strlen(path_cp)-strlen(dr_name)==0){
		parent_ino = 0;
	}else{
		strncpy(parent_path, path_cp, strlen(path_cp)-strlen(dr_name));
		parent_ino = path_inode(parent_path);
	}
	a1fs_inode *parent = (a1fs_inode*)(fs->image + (sb->start_inode) * A1FS_BLOCK_SIZE + parent_ino*64);
	a1fs_extent_block *e_block = (a1fs_extent_block *)(fs->image + (sb->start_extent+parent_ino)* A1FS_BLOCK_SIZE);
	int num_last_entry=(parent->size/256)%16;
	for (int i =0; i<parent->extent_count; i++){
		int flag = 0;
		int start = e_block->extent_array[i].start;
		int count =e_block->extent_array[i].count;
		a1fs_dentry *data_block_test = (a1fs_dentry*)(fs->image + (sb->start_data + start )* A1FS_BLOCK_SIZE);
		(void)data_block_test;
		for (int j = start; j< count+start; j++){
			a1fs_dentry *data_block = (a1fs_dentry*)(fs->image + (sb->start_data + j )* A1FS_BLOCK_SIZE);
			for (int x = 0; x < 16;x++){
				if (strcmp(data_block[x].name, new_dr)==0){
					if(x==num_last_entry-1){
						parent->size=parent->size-256;
						parent->links--;
						flag = 1;
						break;
					}else{
						//remove this directory, move last directory entry to this position.
						a1fs_dentry *lastdr = (a1fs_dentry*)(fs->image+(sb->start_data+e_block->extent_array[parent->extent_count-1].start+e_block->extent_array[parent->extent_count-1].count-1)*A1FS_BLOCK_SIZE+(num_last_entry-1)*256);
						strcpy(data_block[x].name, lastdr->name);
						data_block[x].ino = lastdr->ino;
						parent->size=parent->size-256;
						parent->links--;
						flag = 1;
						break;
					}
				}
			}
			if(flag==1){
				break;
			}
		}
		if(flag==1){
			break;
		}
	}
	return 0;
}


/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();
	//TODO: create a file at given path with given mode
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	sb->free_inodes_count--;
	//find empty inodemj
	if (sb->free_inodes_count == 0 || sb->free_blocks_count == 0){
		return -ENOMEM;	
	}
	bitmap *inode_bm = (bitmap *)(fs->image + sb->start_inode_map*A1FS_BLOCK_SIZE);
	unsigned char ino_bm[4096];
	memcpy(ino_bm, inode_bm->map, sizeof(inode_bm->map));
	int free_inode_ind = 0;
	for (unsigned int bit_byte = 0; bit_byte < A1FS_BLOCK_SIZE; bit_byte++){
		int flag=0;
		unsigned char x1 = ino_bm[bit_byte];
		for(int j = 0; j < 8; j++) {
			unsigned char mask = 1 << (7-j);
			if (!(x1 & mask)) {
				modify(inode_bm, j, 1);
				flag=1;
				break;
			}else{
				free_inode_ind++;
			}
		}
		if (flag==1){
			break;
		}
	}
	//find and update parent
    char path_cp[100];
    strcpy(path_cp, path);
	const char ch = '/';
	char *dr_name = strrchr(path_cp, ch);
	int parent_ino;
	char parent_name[A1FS_NAME_MAX];
	char *new_dr = dr_name+1;
	char parent_path[A1FS_PATH_MAX];
	strncpy(parent_path, path_cp, strlen(path_cp)-strlen(dr_name));
	char *parent_na = strrchr(parent_path, ch);
	if (parent_na == NULL){
		char parent_root[]="..";
		strcpy(parent_name, parent_root);
		parent_ino = 0;
	}else{
		strcpy(parent_name, parent_na+1);
		parent_ino = path_inode(parent_path);
	}
	a1fs_inode *parent = (a1fs_inode*) (fs->image + (sb->start_inode) * A1FS_BLOCK_SIZE+parent_ino*64);
	int extent_count = parent->extent_count;
	a1fs_extent_block *parent_block = (a1fs_extent_block *)(fs->image + (sb->start_extent+parent_ino)* A1FS_BLOCK_SIZE);
	int append_ind = parent_block->extent_array[extent_count-1].start;
	int extent_num = parent_block->extent_array[extent_count-1].count;
	int byte_more = (parent->size)%A1FS_BLOCK_SIZE;
	// create new file
	printf("%s, %s\n", new_dr, parent_name);
	parent->links = (parent->links)+1;
	parent->size = (parent->size)+256;

	a1fs_inode *new_ino = (a1fs_inode*)(fs->image+ (sb->start_inode) * A1FS_BLOCK_SIZE + free_inode_ind*64);
	new_ino->mode = mode;
	new_ino->links = 1;
	new_ino->size = 0;
	new_ino->ino_number = free_inode_ind;
	new_ino->extent_count = 0;

	a1fs_dentry *parent_append=(a1fs_dentry*)(fs->image+(sb->start_data+append_ind+extent_num-1)*A1FS_BLOCK_SIZE+byte_more);
	parent_append->ino = free_inode_ind;
	strcpy(parent_append->name, new_dr);
	return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	// TODO: remove the file at given path
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	// get file inode, updatebitmap
	int file_ino = path_inode(path);
	a1fs_inode *file_inode = (a1fs_inode*) (fs->image + (sb->start_inode) * A1FS_BLOCK_SIZE+file_ino*64);
	bitmap *inode_bm = (bitmap *)(fs->image + sb->start_inode_map*A1FS_BLOCK_SIZE);
	modify(inode_bm, file_ino, 0);
	sb->free_inodes_count++;
	// update data bitmap to 0 of file location 
	for (int x =0; x<file_inode->extent_count; x++){
		bitmap *data_bitmap = (bitmap *)(fs->image + sb->start_data_map*A1FS_BLOCK_SIZE);
		a1fs_extent_block *file_data_block = (a1fs_extent_block *)(fs->image + (sb->start_extent+file_ino)*A1FS_BLOCK_SIZE);
		int start = file_data_block->extent_array[x].start;
		int count = file_data_block->extent_array[x].count;
		for(int d =start; d<start+count; d++ ){
			modify(data_bitmap, d, 0);
			sb->free_blocks_count++;
		}
	}
	//modify its parent
	char path_cp[100];
    strcpy(path_cp, path);
	const char ch = '/';
	char *dr_name = strrchr(path_cp, ch);
	char *new_dr = dr_name+1;
    char parent_path[A1FS_PATH_MAX];
	int parent_ino;
	if (strlen(path_cp)-strlen(dr_name)==0){
		parent_ino = 0;
	}else{
		strncpy(parent_path, path_cp, strlen(path_cp)-strlen(dr_name));
		parent_ino = path_inode(parent_path);
	}
	a1fs_inode *parent = (a1fs_inode*)(fs->image + (sb->start_inode+parent_ino) * A1FS_BLOCK_SIZE);
	a1fs_extent_block *e_block = (a1fs_extent_block *)(fs->image + (sb->start_extent+parent_ino)* A1FS_BLOCK_SIZE);
	parent->links--;
	int num_last_entry=(parent->size/256)%16;
	for (int i =0; i<parent->extent_count; i++){
		int flag = 0;
		int start = e_block->extent_array[i].start;
		int count =e_block->extent_array[i].count;
		for (int j = start; j< count+start; j++){
			a1fs_dentry *data_block = (a1fs_dentry*)(fs->image + (sb->start_data + j )* A1FS_BLOCK_SIZE);
			for (int x = 0; x < 16;x++){
				if (strcmp(data_block[x].name, new_dr)==0){
					if(x==num_last_entry-1){
						parent->size=parent->size-256;
						flag = 1;
						break;
					}else{
						//remove this directory, move last directory entry to this position.
						a1fs_dentry *lastdr = (a1fs_dentry*)(fs->image+(sb->start_data+e_block->extent_array[parent->extent_count-1].start+e_block->extent_array[parent->extent_count-1].count-1)*A1FS_BLOCK_SIZE+(num_last_entry-1)*256);
						strcpy(data_block[x].name, lastdr->name);
						data_block[x].ino = lastdr->ino;
						parent->size=parent->size-256;
						flag = 1;
						break;
					}
				}
			}
			if(flag==1){
				break;
			}
		}
		if(flag==1){
			break;
		}
	}
	return 0; 
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();

	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	int current_ino = path_inode(path);
	a1fs_inode *current = (a1fs_inode*)(fs->image + sb->start_inode * A1FS_BLOCK_SIZE+current_ino*64);
	if (times == NULL){
		struct timespec spec;
		clock_gettime(CLOCK_REALTIME, &spec);
	}else{
		current->mtime = times[1];
	}
	return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	
	bitmap *inode_bm = (bitmap *)(fs->image + sb->start_inode_map*A1FS_BLOCK_SIZE);
	int inode_num= path_inode(path);
	a1fs_inode *inode = (a1fs_inode*)(fs->image + sb->start_inode * A1FS_BLOCK_SIZE+inode_num*64);
	// adding
	int new_size = size - (inode->size % A1FS_BLOCK_SIZE);
	sb->free_blocks_count=sb->free_blocks_count-(new_size/A1FS_BLOCK_SIZE+1);
	int block_count = new_size/A1FS_BLOCK_SIZE;
	int byte_more = inode->size % A1FS_BLOCK_SIZE;
	int index;
	a1fs_extent_block *e_block= (a1fs_extent_block*)(fs->image+ (sb->start_extent + inode_num)*A1FS_BLOCK_SIZE);
	int last_extent;
	if (inode->extent_count == 0) {
		last_extent = find_next_bit(inode_bm);
		modify(inode_bm, last_extent, 1);
		update_extent(inode_num, last_extent);
	}else{
		last_extent = e_block->extent_array[inode->extent_count-1].start +e_block->extent_array[inode->extent_count-1].count;
	}
	
	if ((size - inode->size) > 0){
		unsigned char *zeroing_data_start = (fs->image + (sb->start_data + last_extent )* A1FS_BLOCK_SIZE + byte_more);
		memset(zeroing_data_start, 0, A1FS_BLOCK_SIZE - (inode->size % A1FS_BLOCK_SIZE));
		for (int i=0; i<block_count; i++){
			index = find_next_bit(inode_bm);
			modify(inode_bm, index, 1);
			update_extent(inode_num, index);
			unsigned char *zeroing_data = (fs->image + (sb->start_data + index )* A1FS_BLOCK_SIZE);
			memset(zeroing_data, 0, A1FS_BLOCK_SIZE);
		}
	}else{
		int w = 0;
		int total_data_block = (size / A1FS_BLOCK_SIZE);
		bitmap *data_bitmap = (bitmap *)(fs->image + sb->start_data_map*A1FS_BLOCK_SIZE);
		int result1 = inode->extent_count;
		int result2 = e_block->extent_array[0].count;
		for (int i =0; i<inode->extent_count; i++){
			int start = e_block->extent_array[i].start;
			int count =e_block->extent_array[i].count;
			for (int j = start; j< count+start; j++){
				if(w==total_data_block){
					result1 = i;
					result2 = e_block->extent_array[i].count = j - start + 1;
				}
				if (w>total_data_block){
					modify(data_bitmap, j, 0);
				}
				w++;
			}
		inode->extent_count = result1;
		e_block->extent_array[i].count = result2;
		//e_block->e_array[i].start;
	}
	
	}

	inode->size = size;
	return 0;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: read data from the file at given offset into the buffer
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	int file_ind = path_inode(path);
	a1fs_inode *file_ino = (a1fs_inode*)(fs->image +(sb->start_inode)*A1FS_BLOCK_SIZE+file_ind*64);
	a1fs_extent_block *e_block = (a1fs_extent_block *)(fs->image + (sb->start_extent+file_ind)* A1FS_BLOCK_SIZE);
	// find block where offset locate.
	int file_size = file_ino->size;
	if(offset>file_size){
		return 0;
	}
	int block_front = offset/A1FS_BLOCK_SIZE;
	int begin_byte = offset%A1FS_BLOCK_SIZE;
	int start;
	int count;
	int blocks = 0;
	int i=0;
	while(i<file_ino->extent_count){
		start = e_block->extent_array[i].start;
		count = e_block->extent_array[i].count;
		if (blocks+count > block_front){
			break;
		}else{
			blocks = blocks+count;
		}
		i++;
	}
	memcpy(buf, fs->image + (sb->start_data+start)* A1FS_BLOCK_SIZE + begin_byte, size);
	return size;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   ENOSPC  too many extents (a1fs only needs to support 512 extents per file)
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image);
	bitmap *data_bm = (bitmap *)(fs->image + sb->start_data_map*A1FS_BLOCK_SIZE);
	int inode_num= path_inode(path);
	a1fs_inode *inode = (a1fs_inode*)(fs->image + sb->start_inode * A1FS_BLOCK_SIZE+inode_num*64);
	int write_to;
	a1fs_extent_block *e_block= (a1fs_extent_block*)(fs->image+(sb->start_extent + inode_num)*A1FS_BLOCK_SIZE);
	if (inode->extent_count == 0) {
		// no datablock correspond to file, add extent, allocate a datablock and write to it
		write_to = find_next_bit(data_bm);
		if (write_to==-1){
			return -ENOSPC;
		}
		modify(data_bm, write_to, 1);
		e_block->extent_array[0].start = write_to;
		e_block->extent_array[0].count = 1;
		inode->extent_count= 1;
		sb->free_blocks_count = sb->free_blocks_count-1;
	}else{
		write_to = e_block->extent_array[inode->extent_count-1].start +e_block->extent_array[inode->extent_count-1].count;
	}
	int ori_size=inode->size;
	if (offset>ori_size){
		int byte_more=offset-ori_size;
		unsigned char *zeroing_data_start = (fs->image + (sb->start_data + write_to)* A1FS_BLOCK_SIZE + ori_size);
		memset(zeroing_data_start, 0, byte_more);
		memcpy(fs->image + (sb->start_data+write_to)* A1FS_BLOCK_SIZE+offset, buf, size);
		inode->size = ori_size+size+offset;
	}else{
		memcpy(fs->image + (sb->start_data+write_to)* A1FS_BLOCK_SIZE+offset, buf, size);
		int modify_size = offset+size;
		if(modify_size>ori_size){
			inode->size = offset+size;
		}
	}
	//update file ino
	return size;
}


static struct fuse_operations a1fs_ops = {
	.destroy  = a1fs_destroy,
	.statfs   = a1fs_statfs,
	.getattr  = a1fs_getattr,
	.readdir  = a1fs_readdir,
	.mkdir    = a1fs_mkdir,
	.rmdir    = a1fs_rmdir,
	.create   = a1fs_create,
	.unlink   = a1fs_unlink,
	.utimens  = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read     = a1fs_read,
	.write    = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts)) {
		return 1;
	}

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}
	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
