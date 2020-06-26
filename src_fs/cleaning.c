// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>

//#include "../src_mod/policy/policy.h"
#include "ouichefs.h"
#include "bitmap.h"

extern strategy policy; // Allows to choose the cleaning policy with another module

static const struct inode_operations ouichefs_inode_ops;


/*
 * Calculates if the limit of the usage of the partition has been reached.
 * dir : current directory inode
 * Returns 1 if partition size left is < OUICHEFS_MIN_SPACE, 0 otherwise
 */
int check_limit(struct inode *dir)
{
	uint32_t space;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);

	/* calculate the available space in % */
	space = (sbi->nr_free_blocks % sbi->nr_blocks) ?
		(sbi->nr_free_blocks*100)/sbi->nr_blocks + 1 :
		(sbi->nr_free_blocks*100)/sbi->nr_blocks;

	if (space < OUICHEFS_MIN_SPACE) {
		pr_warning("Critical usage space reached (%u%c) !\n", space, '%');
		return 1;
	}

	pr_info("Partition free space :  %u%c .... OK!\n", space, '%');

	return 0;
}

/*
 * Checks if the dir is full.
 * dir : current directory inode
 * Returns 1 if it is reached, 0 otherwise.
 */
int is_dir_full(struct inode *dir)
{
	struct super_block *sb = dir->i_sb;
	struct ouichefs_inode_info *ci_dir = OUICHEFS_INODE(dir);
	struct buffer_head *bh = NULL;
	struct ouichefs_dir_block *dblock = NULL;
	uint8_t i = 0;

	/* Read the directory index block on disk */
	bh = sb_bread(sb, ci_dir->index_block);
	if (!bh)
		return -EIO;

	dblock = (struct ouichefs_dir_block *)bh->b_data;

	/* Count the number of files in this directory (block) */
	while (i < OUICHEFS_MAX_SUBFILES && dblock->files[i].inode != 0)
		++i;

	brelse(bh);

	if (i < OUICHEFS_MAX_SUBFILES) {
		pr_info("Subfiles quantity in this directory : %u .... OK!\n", i);
		return 0;
	}

	pr_warning("Too many files in this directory !! (%u) !\n", i);

	return 1;
}

/*
 * Get the inode->i_ino of the parent directory
 * dir : current directory inode
 * Returns parent's ino if success, 0 otherwise.
 */
static unsigned long find_parent_of_ino(struct inode *dir,
				unsigned long inoParent,
				unsigned long inoToFind)
{
	unsigned long ino = inoParent, i = 0, res = 0;
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh = NULL;
	struct ouichefs_dir_block *dir_block = NULL;
	struct inode *inode = NULL;

	inode = ouichefs_iget(sb, ino);

	/* Read parent directory index and put in RAM from the disk*/
	bh = sb_bread(sb, OUICHEFS_INODE(inode)->index_block);

	/* Get the block ifself */
	dir_block = (struct ouichefs_dir_block *)bh->b_data;

	/* Search for inode in parent index and get number of subfiles */
	while (i < OUICHEFS_MAX_SUBFILES && dir_block->files[i].inode != 0) {
		ino = dir_block->files[i].inode;
		inode = ouichefs_iget(sb, ino);

		if (S_ISDIR(inode->i_mode)) {
			pr_info("Scanning directory : /%s\n",
						 dir_block->files[i].filename);
			res = find_parent_of_ino(dir, ino, inoToFind);

			if (res > 0) {
				iput(inode);
				return res;
			}
		} else if (ino == inoToFind) {
			pr_info("Targeted file : %s with ino:%lu located!\n",
					    dir_block->files[i].filename, ino);
			iput(inode);
			return inoParent; // on renvoie le parent
		}
		++i;
		iput(inode);
	}

	brelse(bh);

	return 0;
}

/*
 * Get the inode->i_ino of the oldest file in dir
 * dir : current directory inode
 * Returns ino if success, 0 otherwise.
 */
 unsigned long oldest_in_dir(struct inode *dir)
{
	unsigned long ino, i = 0, min = ULONG_MAX, ino_ancien = 0;
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh = NULL;
	struct ouichefs_dir_block *dir_block = NULL;
	struct inode *inode = NULL;

	/* Read parent directory index and put in RAM from the disk*/
	bh = sb_bread(sb, OUICHEFS_INODE(dir)->index_block);

	/* Get the block ifself */
	dir_block = (struct ouichefs_dir_block *)bh->b_data;

	/* Search for inode in parent index and get number of subfiles */
	while (i < OUICHEFS_MAX_SUBFILES && dir_block->files[i].inode != 0) {
		ino = dir_block->files[i].inode;
		inode = ouichefs_iget(sb, ino);

		// is it a directory OR (counter > 2 because there is a dentry)
		//		   OR (counter > 1 because there is no dentry)
		// is it a directory, or used by at least one user process
		if (S_ISDIR(inode->i_mode) ||
		(inode->i_dentry.first != NULL && inode->i_count.counter > 2) ||
		(inode->i_dentry.first == NULL && inode->i_count.counter > 1)) {
			iput(inode);
			++i;
			continue;
		}

		/* check if it's the oldest */
                pr_info("nsec = %ld - sec = %lld\n",inode->i_mtime.tv_nsec,inode->i_mtime.tv_sec);
		if (inode->i_mtime.tv_sec < min) {
			min = inode->i_mtime.tv_sec;
			ino_ancien = ino;
		}
		iput(inode);
		++i;
	}
	brelse(bh);

	pr_info("Le plus ancien du dossier est l'ino #%lu\n", ino_ancien);

	return ino_ancien;
}

/*
 * Get the inode->i_ino of the oldest inode in partition (date of last modif)
 * dir : current directory inode
 * Returns ino if success, 0 otherwise.
 */
 unsigned long oldest_in_partition(struct inode *dir)
{
	struct inode *inode = NULL;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);
	signed long ino = 0, min = LONG_MAX, ino_ancien = 0;

	while (++ino < sbi->nr_inodes) {
		ino = find_next_zero_bit(sbi->ifree_bitmap, sbi->nr_inodes, ino);

		if (ino < sbi->nr_inodes) {
			inode = ouichefs_iget(dir->i_sb, ino);

			// is it a directory OR (counter > 2 because these is a dentry)
			//		   OR (counter > 1 because there is no dentry)
			// is it a director, or used by at least one user process
			if (S_ISDIR(inode->i_mode) /*||
			(inode->i_dentry.first != NULL && inode->i_count.counter > 2) ||
			(inode->i_dentry.first == NULL && inode->i_count.counter > 1)*/) {
				iput(inode);
				continue;
			}

			/* check if it is the oldest */
                        pr_info("nsec = %ld - sec = %lld\n",inode->i_mtime.tv_nsec,inode->i_mtime.tv_sec);
			if (inode->i_mtime.tv_sec < min) {
				min = inode->i_mtime.tv_sec;
				ino_ancien = ino;
			}
			iput(inode);
		}
	}

	pr_info("The partition's oldest file is ino #%lu\n", ino_ancien);

	return ino_ancien;
}

///*
// * Get the inode->i_ino of the biggest inode in dir
// * dir : current directory inode
// * Returns ino of the biggest, the bloc's first inode if they are all equal.
// */
//static unsigned long biggest_in_dir(struct inode *dir)
//{
//	unsigned long ino, i = 0, max = 0, ino_biggest = 1;
//	struct super_block *sb = dir->i_sb;
//	struct buffer_head *bh = NULL;
//	struct ouichefs_dir_block *dir_block = NULL;
//	struct inode *inode = NULL;
//
//	/* Read parent directory index and put in RAM from the disk*/
//	bh = sb_bread(sb, OUICHEFS_INODE(dir)->index_block);
//
//	/* Get the block ifself */
//	dir_block = (struct ouichefs_dir_block *)bh->b_data;
//
//	/* Search for inode in parent index and get number of subfiles */
//	while (i < OUICHEFS_MAX_SUBFILES && dir_block->files[i].inode != 0) {
//
//		ino = dir_block->files[i].inode;
//		inode = ouichefs_iget(sb, ino);
//
//		// is it a directory OR (counter > 2 because these is a dentry)
//		//		   OR (counter > 1 because there is no dentry)
//		// is it a director, or used by at least one user process
//		if (S_ISDIR(inode->i_mode) ||
//		(inode->i_dentry.first != NULL && inode->i_count.counter > 2) ||
//		(inode->i_dentry.first == NULL && inode->i_count.counter > 1)) {
//			iput(inode);
//			++i;
//			continue;
//		}
//
//		/* by default, we choose the first inode is the block
//		   in case all the files are empty. */
//		if (inode->i_size > max || i == 0) {
//			max = inode->i_size;
//			ino_biggest = ino;
//		}
//		iput(inode);
//		++i;
//	}
//	brelse(bh);
//	pr_info("The biggest file in this dir is ino#%lu\n", ino_biggest);
//
//	return ino_biggest;
//}
//
///*
// * Get the inode->i_ino of the biggest inode in partition
// * dir : current directory inode
// * Returns ino of the biggest, 0 otherwise.
// */
//static unsigned long biggest_in_partition(struct inode *dir)
//{
//	struct inode *inode = NULL;
//	struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);
//	unsigned long ino = 0, max = 0, ino_ancien = 0;
//
//	while (++ino < sbi->nr_inodes) {
//		ino = find_next_zero_bit(sbi->ifree_bitmap, sbi->nr_inodes, ino);
//
//		if (ino < sbi->nr_inodes) {
//			inode = ouichefs_iget(dir->i_sb, ino);
//
//			// is it a directory OR (counter > 2 because these is a dentry)
//			//		   OR (counter > 1 because there is no dentry)
//			// is it a director, or used by at least one user process
//			if (S_ISDIR(inode->i_mode) ||
//			(inode->i_dentry.first != NULL && inode->i_count.counter > 2) ||
//			(inode->i_dentry.first == NULL && inode->i_count.counter > 1)) {
//				iput(inode);
//				continue;
//			}
//
//			/* check if it is the biggest */
//			if (inode->i_size > max) {
//				max = inode->i_size;
//				ino_ancien = ino;
//			}
//			iput(inode);
//		}
//	}
//
//	pr_info("The biggest file is ino #%lu\n", ino_ancien);
//
//	return ino_ancien;
//}
//
/*
 * Free the dir of the inode and free the blocks of the inode
 * dir : current directory of the file
 * flag : 0 search for parent, 1 parent is dir
 */
static int shred_it(struct inode *dir, unsigned long ino, TypePolicy flag)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inodeToDelete = NULL, *inodeParent = dir;
	struct dentry *dentry = NULL;
        int res;
	unsigned long parent = 0;

	inodeToDelete = ouichefs_iget(sb, ino);
	iput(inodeToDelete);

	if (inodeToDelete->i_dentry.first != NULL) { // dentry in cache
		dentry = hlist_entry(inodeToDelete->i_dentry.first,
					   struct dentry, d_u.d_alias);
                
	        /* We use unlink because there is a dentry in cache */
                res = ouichefs_unlink(dentry->d_parent->d_inode, dentry);
                
                /* invalidate to be able to create same filename */ 
                                      
                d_invalidate(dentry);

                // fait chier au rmmod mais 
                // 
                iput(inodeToDelete);
                // mark_inode_dirty_sync(inodeToDelete);
                pr_info("icount = %d\n", inodeToDelete->i_count.counter);

	} else {   // no dentry in cache
		if (flag == tp_partition) {
			parent = find_parent_of_ino(dir, 0, ino);
			inodeParent = ouichefs_iget(sb, parent);
			iput(inodeParent);
		}
		pr_info("Parent's directory located, ino : %lu\n", parent);
		res = scrub_and_clean(inodeParent, inodeToDelete);
	}

	return res;
}

/*
 * Free the partition or the dir of the oldest file.
 * dir : current directory
 * flag : partition or directory
 */
//int clean_it(struct inode *dir, TypePolicy flag)
//{
//	unsigned long ino = 0;
//
//	/* policy refers to global module variable */
//	/* can be changed by inserting another module */
//	if (policy == tp_oldest)
//		ino = (flag == tp_directory) ? oldest_in_dir(dir) :
//					    oldest_in_partition(dir);
//	else if (policy == tp_biggest)
//		ino = (flag == tp_directory) ? biggest_in_dir(dir) :
//					    biggest_in_partition(dir);
//
//	if (!ino) {
//		pr_warning("Error, cannot delete the ino#0 (patition's root!");
//		return 1;
//	}
//
//	return shred_it(dir, ino, flag);
//}

int clean_it(struct inode *dir, TypePolicy flag)
{
	unsigned long ino = 0;

	/* policy refers to global module variable */
	/* can be changed by inserting another module */
	
		ino = (flag == tp_directory) ? policy.inDir(dir) :
					    policy.inPartition(dir);
	

	if (!ino) {
		pr_warning("Error, cannot delete the ino#0 (patition's root!");
		return 1;
	}

	return shred_it(dir, ino, flag);
}
