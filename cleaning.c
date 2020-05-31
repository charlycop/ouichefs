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
 * Calculate the usage % of the partition
 * dir : current directory inode
 * and return 1 if it is too much, 0 if everything is under control
 */
ssize_t isPartitionFull(struct inode *dir)
{       
        uint32_t space;
        struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);
             
        space = (sbi->nr_free_blocks*100)/sbi->nr_blocks;
        
        if(sbi->nr_blocks % sbi->nr_free_blocks)
                --space;
                
        if (space < OUICHEFS_MIN_SPACE){
                pr_warning("Limite de stockage critique (%u%c) !\n", space,'%');
                return 1;
        }
        
        pr_info("Partition free space :  %u%c .... OK!\n", space,'%');

        return 0;
} 

/*
 * Check if the OUICHEFS_MAX_SUBFILES is reached.
 * dir : current directory inode
 * and return 1 if it is reached, 0 is not.
 */
ssize_t isDirFull(struct inode *dir)
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

unsigned long findParentOfIno(struct inode *dir, unsigned long inoParent,
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
                        pr_info("On entre dans dossier : /%s\n", 
                                                 dir_block->files[i].filename);
                        res = findParentOfIno(dir, ino, inoToFind);
                        
                        if(res > 0){
                                iput(inode);
                                return res;
                        }
                }
                else if (ino == inoToFind){
                        pr_info("fichier : %s avec ino:%lu localisé!\n",
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

unsigned long findOldestInDir(struct inode *dir)
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


                pr_info("counter (apres get) : %d\n", inode->i_count.counter);
                //pr_info("inode->i_dio_count.counter : %d\n", inode->i_dio_count.counter);
                //pr_info("inode->i_nlink : %u\n", inode->i_nlink);

                // si pas fichier ou utilisé
                if (S_ISDIR(inode->i_mode) || inode->i_count.counter > 1){
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

unsigned long findOldestInPartition(struct inode *dir)
{
        struct inode *inode = NULL;
        struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);
        unsigned long ino = 0, min = ULONG_MAX, ino_ancien = 0;
        

        while(++ino < sbi->nr_inodes){
                ino = find_next_zero_bit(sbi->ifree_bitmap, sbi->nr_inodes, ino);
                
                if(ino < sbi->nr_inodes){
                        inode = ouichefs_iget(dir->i_sb, ino);
                         // si pas fichier ou utilisé
                        if (S_ISDIR(inode->i_mode) || inode->i_count.counter > 1){
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

        pr_info("Le plus ancien de la partition est l'ino #%lu\n", ino_ancien);

        return ino_ancien;
}

unsigned long findBigestInDir(struct inode *dir)
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

                // si pas fichier ou utilisé
                if (S_ISDIR(inode->i_mode) || inode->i_count.counter > 1){
                      ++i;
                      iput(inode);
                      continue;    
                }
                
                if(inode->i_size > max){
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

unsigned long findBigestInPartition(struct inode *dir)
{
        struct inode *inode = NULL;
        struct ouichefs_sb_info *sbi = OUICHEFS_SB(dir->i_sb);
        unsigned long ino = 0, max = 0, ino_ancien = 0;
        
	pr_info("je suis dans le findBigest\n");

        while(++ino < sbi->nr_inodes){
                ino = find_next_zero_bit(sbi->ifree_bitmap, sbi->nr_inodes, ino);
                
                if(ino < sbi->nr_inodes){
                        inode = ouichefs_iget(dir->i_sb, ino);
                        
                        // si pas fichier ou deja utilisé
                        if (S_ISDIR(inode->i_mode) || inode->i_count.counter > 1){ 
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
ssize_t shredIt(struct inode *dir, unsigned long ino, TypePolicy flag){
      
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
                        parent = findParentOfIno(dir, 0, ino);
                        inodeParent = ouichefs_iget(sb, parent);
                        iput(inodeParent);
                }
                pr_info("Parent trouvé ino : %lu\n", parent);
                return scrubAndClean(inodeParent, inodeToDelete);
        }    
        
        return 1;  
}

/*
 * Free the partition or the dir of the oldest file.
 * dir : current directory
 * flag : 0 for partition, 1 for dir
 */
ssize_t cleanIt(struct inode *dir, TypePolicy flag)
{
        unsigned long ino = 0;

        pr_info ("valeur de policy.val %d\n",policy);

	if(policy == oldest)
                ino = (flag == directory) ? findOldestInDir(dir) : findOldestInPartition(dir);
	else if (policy == biggest)
                ino = (flag == directory) ? findBigestInDir(dir) : findBigestInPartition(dir);

        if(!ino){
                pr_warning("Error, cannot retrieve the inode to delete!");
                return 1;
        }

        return shredIt(dir, ino, flag);
}
