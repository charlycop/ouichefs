#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include "../../src_fs/ouichefs.h"
//#include "policy.h"


extern strategy policy;

MODULE_DESCRIPTION("Module for policy modification in OUICHEFS");
MODULE_AUTHOR("Hugo Lopes, Pierre Boeshertz, Charly Oudy, SESI M1 2020");
MODULE_LICENSE("GPL");



/*
 * Get the inode->i_ino of the biggest inode in dir
 * dir : current directory inode
 * Returns ino of the biggest, the bloc's first inode if they are all equal.
 */
unsigned long biggest_in_dir(struct inode *dir)
{
	unsigned long ino, i = 0, max = 0, ino_biggest = 1;
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

		// is it a directory OR (counter > 2 because these is a dentry)
		//		   OR (counter > 1 because there is no dentry)
		// is it a director, or used by at least one user process
		if (S_ISDIR(inode->i_mode) ||
		(inode->i_dentry.first != NULL && inode->i_count.counter > 2) ||
		(inode->i_dentry.first == NULL && inode->i_count.counter > 1)) {
			iput(inode);
			++i;
			continue;
		}

		/* by default, we choose the first inode is the block
		   in case all the files are empty. */
		if (inode->i_size > max || i == 0) {
			max = inode->i_size;
			ino_biggest = ino;
		}
		iput(inode);
		++i;
	}
	brelse(bh);
	pr_info("The biggest file in this dir is ino#%lu\n", ino_biggest);

	return ino_biggest;
}

/*
 * Get the inode->i_ino of the biggest inode in partition
 * dir : current directory inode
 * Returns ino of the biggest, 0 otherwise.
 */
unsigned long biggest_in_partition(struct inode *dir)
{
	struct inode *inode = NULL;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);
	unsigned long ino = 0, max = 0, ino_ancien = 0;

	while (++ino < sbi->nr_inodes) {
		ino = find_next_zero_bit(sbi->ifree_bitmap, sbi->nr_inodes, ino);

		if (ino < sbi->nr_inodes) {
			inode = ouichefs_iget(dir->i_sb, ino);

			// is it a directory OR (counter > 2 because these is a dentry)
			//		   OR (counter > 1 because there is no dentry)
			// is it a director, or used by at least one user process
			if (S_ISDIR(inode->i_mode) ||
			(inode->i_dentry.first != NULL && inode->i_count.counter > 2) ||
			(inode->i_dentry.first == NULL && inode->i_count.counter > 1)) {
				iput(inode);
				continue;
			}

			/* check if it is the biggest */
			if (inode->i_size > max) {
				max = inode->i_size;
				ino_ancien = ino;
			}
			iput(inode);
		}
	}

	pr_info("The biggest file is ino #%lu\n", ino_ancien);

	return ino_ancien;
}


static strategy policy_old;

static int __init hello_init(void)
{
        policy_old.inDir =policy.inDir;
        policy_old.inPartition=policy.inPartition;
	policy.inDir = biggest_in_dir;
        policy.inPartition = biggest_in_partition;
	pr_info("Changing cleaning policy : BIGGEST FILE\n");
	return 0;
}
module_init(hello_init);

static void __exit hello_exit(void)
{
	policy.inDir = policy_old.inDir;
        policy.inPartition = policy_old.inPartition;
	pr_info("Changing cleaning policy : OLDEST FILE\n");
}
module_exit(hello_exit);
