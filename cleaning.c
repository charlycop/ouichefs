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

#include "size.h"     
#include "ouichefs.h"
#include "bitmap.h"

extern TypePolicy policy; // Allows to choose the cleaning policy with another module

static const struct inode_operations ouichefs_inode_ops;



/*
 * Calculates if the limit of the usage of the partition has been reached.
 * dir : current directory inode
 * Returns 1 if partition size left is < OUICHEFS_MIN_SPACE, 0 otherwise
 */

// mauvais nom ? on cherche pas si elle est full, mais si elle a dépassée notre limite 
// plutot check_limit() ?
// j'ai mis int car c'est le plus proche du bool en c
int is_partition_full(struct inode *dir)
{       
        uint32_t space;
        struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);
             
        space = (sbi->nr_free_blocks*100)/sbi->nr_blocks;
        
        if(sbi->nr_blocks % sbi->nr_free_blocks)
                --space;

        // j'ai ça en une ligne, plutot que le if (free ->space), mais ca dépasse les 80 char par ligne
        // free = (sbi->nr_free_blocks % sbi->nr_blocks) ? (sbi->nr_free_blocks*100)/sbi->nr_blocks + 1 : (sbi->nr_free_blocks*100)/sbi->nr_blocks;
                
        if (space < OUICHEFS_MIN_SPACE){
                pr_warning("Critical usage space reached (%u%c) !\n", space,'%');
                return 1;
        }
        
        pr_info("Partition free space :  %u%c .... OK!\n", space,'%');

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
	while(i < OUICHEFS_MAX_SUBFILES && dblock->files[i].inode != 0)
                ++i;
	
	brelse(bh);

        if (i < OUICHEFS_MAX_SUBFILES){
                pr_info("Subfiles quantity in this directory : %u .... OK!\n", i);
                return 0;
        }
        
        pr_warning("Too many files in this directory !! (%u) !\n", i);

        return 1;
} 

//static puisque utilisée nulle part ailleurs ?

unsigned long find_parent_of_ino(struct inode *dir, unsigned long inoParent,
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
        while(i < OUICHEFS_MAX_SUBFILES && dir_block->files[i].inode != 0){               
                ino = dir_block->files[i].inode;
                inode = ouichefs_iget(sb, ino);  
              
                if (S_ISDIR(inode->i_mode)){
                        pr_info("Scanning directory : /%s\n", 
                                                 dir_block->files[i].filename);
                        res = find_parent_of_ino(dir, ino, inoToFind);
                        
                        if(res > 0){
                                iput(inode);
                                return res;
                        }
                }
                else if (ino == inoToFind){
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

//static puisque utilisée nulle part ailleurs ?

unsigned long find_oldest_in_dir(struct inode *dir)
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
        while(i < OUICHEFS_MAX_SUBFILES && dir_block->files[i].inode != 0){               
            
                ino = dir_block->files[i].inode;
                inode = ouichefs_iget(sb, ino);  

//                if(inode->i_dentry.first != NULL)
//                        pr_info("avec dentry : i_count=%d\n", inode->i_count.counter);
//                else
//                        pr_info("pas dentry : i_count=%d\n", inode->i_count.counter);
                
                // is it a directory OR (counter > 2 because these is a dentry)
                //                   OR (counter > 1 because there is no dentry)
                // is it a director, or used by at least one user process
                if (S_ISDIR(inode->i_mode) ||
                (inode->i_dentry.first != NULL && inode->i_count.counter > 2) || 
                (inode->i_dentry.first == NULL && inode->i_count.counter > 1)){
                        iput(inode);
                        ++i;
                        continue;
                }

                //pr_info("dir->i_count.counter : %d\n", dir->i_count.counter);

                if(inode->i_mtime.tv_sec < min){
                        min = inode->i_mtime.tv_sec;
                        ino_ancien = ino;
                }
                //pr_info("inode->i_count.counter(avant) : %d\n", inode->i_count.counter);
                iput(inode);
                //pr_info("inode->i_count.counter(apres) : %d\n", inode->i_count.counter);
                ++i;       
	}
        brelse(bh);

        pr_info("Le plus ancien du dossier est l'ino #%lu\n", ino_ancien);

        return ino_ancien;
}

//static puisque utilisée nulle part ailleurs ?
// j'ai changé le nom qui me paraissait un peu trop long (le find n'est pas utile non ?)

unsigned long oldest_in_partition(struct inode *dir)
{
        struct inode *inode = NULL;
        struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);
        unsigned long ino = 0, min = ULONG_MAX, ino_ancien = 0;
        

        while(++ino < sbi->nr_inodes){
                ino = find_next_zero_bit(sbi->ifree_bitmap, sbi->nr_inodes, ino);
                
                if(ino < sbi->nr_inodes){
                        inode = ouichefs_iget(dir->i_sb, ino);
                        
//                        if(inode->i_dentry.first != NULL)
//                                pr_info("avec dentry : i_count=%d\n", inode->i_count.counter);
//                        else
//                                pr_info("pas dentry : i_count=%d\n", inode->i_count.counter);
                        
                        // is it a directory OR (counter > 2 because these is a dentry)
                        //                   OR (counter > 1 because there is no dentry)
                        // is it a director, or used by at least one user process
                        if (S_ISDIR(inode->i_mode) ||
                        (inode->i_dentry.first != NULL && inode->i_count.counter > 2) || 
                        (inode->i_dentry.first == NULL && inode->i_count.counter > 1)){
                                iput(inode);
                                continue;
                        }


                        if(inode->i_mtime.tv_sec < min){
                                min = inode->i_mtime.tv_sec;
                                ino_ancien = ino;
                        }
                        iput(inode);
                }
        }

        pr_info("The partition's oldest file is ino #%lu\n", ino_ancien);

        return ino_ancien;
}

//static puisque utilisée nulle part ailleurs ?
// j'ai changé le nom qui me paraissait un peu trop long (le find n'est pas utile non ?)


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
        while(i < OUICHEFS_MAX_SUBFILES && dir_block->files[i].inode != 0){ 

                ino = dir_block->files[i].inode;
                inode = ouichefs_iget(sb, ino);  
                                        
//
//                if(inode->i_dentry.first != NULL)
//                        pr_info("avec dentry : i_count=%d\n", inode->i_count.counter);
//                else
//                        pr_info("pas dentry : i_count=%d\n", inode->i_count.counter);

                
                // is it a directory OR (counter > 2 because these is a dentry)
                //                   OR (counter > 1 because there is no dentry)
                // is it a director, or used by at least one user process
                if (S_ISDIR(inode->i_mode) ||
                (inode->i_dentry.first != NULL && inode->i_count.counter > 2) || 
                (inode->i_dentry.first == NULL && inode->i_count.counter > 1)){
                        iput(inode);
                        ++i;
                        continue;
                }
                
                /* by default, we choose the first inode is the block
                   in case all the files are empty.    */         
                if(inode->i_size > max || i == 0){
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

//static puisque utilisée nulle part ailleurs ?
// j'ai changé le nom qui me paraissait un peu trop long (le find n'est pas utile non ?)


unsigned long biggest_in_partition(struct inode *dir)
{
        struct inode *inode = NULL;
        struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);
        unsigned long ino = 0, max = 0, ino_ancien = 0;
        

        while(++ino < sbi->nr_inodes){
                ino = find_next_zero_bit(sbi->ifree_bitmap, sbi->nr_inodes, ino);
                
                if(ino < sbi->nr_inodes){
                        inode = ouichefs_iget(dir->i_sb, ino);
                        
//                        if(inode->i_dentry.first != NULL)
//                                pr_info("avec dentry : i_count=%d\n", inode->i_count.counter);
//                        else
//                                pr_info("pas dentry : i_count=%d\n", inode->i_count.counter);
                        
                        // is it a directory OR (counter > 2 because these is a dentry)
                        //                   OR (counter > 1 because there is no dentry)
                        // is it a director, or used by at least one user process
                        if (S_ISDIR(inode->i_mode) ||
                        (inode->i_dentry.first != NULL && inode->i_count.counter > 2) || 
                        (inode->i_dentry.first == NULL && inode->i_count.counter > 1)){
                                iput(inode);
                                continue;
                        }


                        if(inode->i_size > max){
                                max = inode->i_size;
                                ino_ancien = ino;
                        }
                        iput(inode);
                }
        }

        pr_info("The biggest file is ino #%lu\n", ino_ancien);

        return ino_ancien;
}

/*
 * Free the dir of the inode and free the blocks of the inode
 * dir : current directory of the file
 * flag : 0 search for parent, 1 parent is dir
 */

// static aussi ? nulle part ailleurs
int shred_it(struct inode *dir, unsigned long ino, TypePolicy flag){
      
        struct super_block *sb = dir->i_sb;
        struct inode *inodeToDelete = NULL, *inodeParent = dir;
        struct dentry *dentry = NULL;
        unsigned long parent = 0;

        inodeToDelete = ouichefs_iget(sb, ino);     
        iput(inodeToDelete);                

        if(inodeToDelete->i_dentry.first != NULL){ // si dentry : on fait unlink direct
                dentry = hlist_entry(inodeToDelete->i_dentry.first, 
                                           struct dentry, d_u.d_alias);
                // DILEMME UTILISE-T-ON UNLINK pour montrer qu'on a bien capter la diff' ?
                return ouichefs_unlink(dentry->d_parent->d_inode, dentry);
        }
        else{  
                if(flag == partition){
                        parent = find_parent_of_ino(dir, 0, ino);
                        inodeParent = ouichefs_iget(sb, parent);
                        iput(inodeParent);
                }
                pr_info("Parent's directory located, ino : %lu\n", parent);
                return scrub_and_clean(inodeParent, inodeToDelete);
        }    
        
        return 1;  
}

/*
 * Free the partition or the dir of the oldest file.
 * dir : current directory
 * flag : 0 for partition, 1 for dir
 */
int clean_it(struct inode *dir, TypePolicy flag)
{
        unsigned long ino = 0;

        //pr_info ("valeur de policy.val %d\n",policy);

	if(policy == oldest)
                ino = (flag == directory) ? find_oldest_in_dir(dir) : oldest_in_partition(dir);
	else if (policy == biggest)
                ino = (flag == directory) ? biggest_in_dir(dir) : biggest_in_partition(dir);

        if(!ino){
                pr_warning("Error, cannot retrieve the inode to delete!");
                return 1;
        }

        return shred_it(dir, ino, flag);
}
