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

#include "size.h"     // 
#include "ouichefs.h"
#include "bitmap.h"
//#include "cleaning.h" // Partition cleaning tools and policies

extern policies policy; // Allows to choose the cleaning policy with another module

static const struct inode_operations ouichefs_inode_ops;

/*
 * Get inode ino from disk.
 */
struct inode *ouichefs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode = NULL;
	struct ouichefs_inode *cinode = NULL;
	struct ouichefs_inode_info *ci = NULL;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct buffer_head *bh = NULL;
	uint32_t inode_block = (ino / OUICHEFS_INODES_PER_BLOCK) + 1;
	uint32_t inode_shift = ino % OUICHEFS_INODES_PER_BLOCK;
	int ret;

	/* Fail if ino is out of range */
	if (ino >= sbi->nr_inodes)
		return ERR_PTR(-EINVAL);
        
	/* Get a locked inode from Linux */
	inode = iget_locked(sb, ino);
        
	if (!inode)
		return ERR_PTR(-ENOMEM);
	/* If inode is in cache, return it */
	if (!(inode->i_state & I_NEW))
		return inode;

	ci = OUICHEFS_INODE(inode);

	/* Read inode from disk and initialize */
	bh = sb_bread(sb, inode_block);

	if (!bh) {
		ret = -EIO;
		goto failed;
	}
	cinode = (struct ouichefs_inode *)bh->b_data;
	cinode += inode_shift;

	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_op = &ouichefs_inode_ops;

	inode->i_mode = le32_to_cpu(cinode->i_mode);
	i_uid_write(inode, le32_to_cpu(cinode->i_uid));
	i_gid_write(inode, le32_to_cpu(cinode->i_gid));
	inode->i_size = le32_to_cpu(cinode->i_size);
	inode->i_ctime.tv_sec = (time64_t)le32_to_cpu(cinode->i_ctime);
	inode->i_ctime.tv_nsec = 0;
	inode->i_atime.tv_sec = (time64_t)le32_to_cpu(cinode->i_atime);
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_sec = (time64_t)le32_to_cpu(cinode->i_mtime);
	inode->i_mtime.tv_nsec = 0;
	inode->i_blocks = le32_to_cpu(cinode->i_blocks);
	set_nlink(inode, le32_to_cpu(cinode->i_nlink));
	ci->index_block = le32_to_cpu(cinode->index_block);

	if (S_ISDIR(inode->i_mode)) {
		inode->i_fop = &ouichefs_dir_ops;
	} else if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &ouichefs_file_ops;
		inode->i_mapping->a_ops = &ouichefs_aops;
	}

	brelse(bh);

	/* Unlock the inode to make it usable */
	unlock_new_inode(inode);

	return inode;

failed:
	brelse(bh);
	iget_failed(inode);
	return ERR_PTR(ret);
}

/*
 * Look for dentry in dir.
 * Fill dentry with NULL if not in dir, with the corresponding inode if found.
 * Returns NULL on success.
 */
static struct dentry *ouichefs_lookup(struct inode *dir, struct dentry *dentry,
				      unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	struct ouichefs_inode_info *ci_dir = OUICHEFS_INODE(dir);
	struct inode *inode = NULL;
	struct buffer_head *bh = NULL;
	struct ouichefs_dir_block *dblock = NULL;
	struct ouichefs_file *f = NULL;
	int i;

	/* Check filename length */
	if (dentry->d_name.len > OUICHEFS_FILENAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	/* Read the directory index block on disk */
	bh = sb_bread(sb, ci_dir->index_block);
	if (!bh)
		return ERR_PTR(-EIO);
	dblock = (struct ouichefs_dir_block *)bh->b_data;

	/* Search for the file in directory */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		f = &dblock->files[i];
		if (!f->inode)
			break;
		if (!strncmp(f->filename, dentry->d_name.name,
			     OUICHEFS_FILENAME_LEN)) {
			inode = ouichefs_iget(sb, f->inode);
			break;
		}
	}
	brelse(bh);

	/* Update directory access time */
	dir->i_atime = current_time(dir);
	mark_inode_dirty(dir);

	/* Fill the dentry with the inode */
	d_add(dentry, inode);

	return NULL;
}

/*
 * Create a new inode in dir.
 */
static struct inode *ouichefs_new_inode(struct inode *dir, mode_t mode)
{
	struct inode *inode;
	struct ouichefs_inode_info *ci;
	struct super_block *sb;
	struct ouichefs_sb_info *sbi;
	uint32_t ino, bno;
	int ret;

	/* Check mode before doing anything to avoid undoing everything */
	if (!S_ISDIR(mode) && !S_ISREG(mode)) {
		pr_err("File type not supported (only directory and regular files supported)\n");
		return ERR_PTR(-EINVAL);
	}

	/* Check if inodes are available */
	sb = dir->i_sb;
	sbi = OUICHEFS_SB(sb);
	if (sbi->nr_free_inodes == 0 || sbi->nr_free_blocks == 0)
		return ERR_PTR(-ENOSPC);

	/* Get a new free inode */
	ino = get_free_inode(sbi);
	if (!ino)
		return ERR_PTR(-ENOSPC);
	inode = ouichefs_iget(sb, ino);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto put_ino;
	}
	ci = OUICHEFS_INODE(inode);

	/* Get a free block for this new inode's index */
	bno = get_free_block(sbi);
	if (!bno) {
		ret = -ENOSPC;
		goto put_inode;
	}
	ci->index_block = bno;

	/* Initialize inode */
	inode_init_owner(inode, dir, mode);
	inode->i_blocks = 1;
	if (S_ISDIR(mode)) {
		inode->i_size = OUICHEFS_BLOCK_SIZE;
		inode->i_fop = &ouichefs_dir_ops;
		set_nlink(inode, 2); /* . and .. */
	} else if (S_ISREG(mode)) {
		inode->i_size = 0;
		inode->i_fop = &ouichefs_file_ops;
		inode->i_mapping->a_ops = &ouichefs_aops;
		set_nlink(inode, 1);
	}

	inode->i_ctime = inode->i_atime = inode->i_mtime = current_time(inode);

	return inode;

put_inode:
	iput(inode);
put_ino:
	put_inode(sbi, ino);

	return ERR_PTR(ret);
}

/*
 * Remove a link for a file. If link count is 0, destroy file in this way:
 *   - remove the file from its parent directory.
 *   - cleanup blocks containing data
 *   - cleanup file index block
 *   - cleanup inode
 */
int scrubAndClean(struct inode *parentInode, struct inode *childInode)
{
        struct super_block *sb = parentInode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct buffer_head *bh = NULL, *bh2 = NULL;
	struct ouichefs_dir_block *dir_block = NULL;
	struct ouichefs_file_index_block *file_block = NULL;
	uint32_t ino, bno;
	int i, f_id = -1, nr_subs = 0;

        ino = childInode->i_ino;
        bno = OUICHEFS_INODE(childInode)->index_block;

        /* Read parent directory index */
	bh = sb_bread(sb, OUICHEFS_INODE(parentInode)->index_block);
	if (!bh)
		return -EIO;
	dir_block = (struct ouichefs_dir_block *)bh->b_data;

	/* Search for inode in parent index and get number of subfiles */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		if (dir_block->files[i].inode == ino)
			f_id = i;
		else if (dir_block->files[i].inode == 0)
			break;
	}
	nr_subs = i;
        
	/* Remove file from parent directory */
	if (f_id != OUICHEFS_MAX_SUBFILES - 1)
		memmove(dir_block->files + f_id,
			dir_block->files + f_id + 1,
			(nr_subs - f_id - 1) * sizeof(struct ouichefs_file));
	memset(&dir_block->files[nr_subs - 1],
	       0, sizeof(struct ouichefs_file));
	mark_buffer_dirty(bh);
	brelse(bh);

	/* Update inode stats */
	parentInode->i_mtime = parentInode->i_atime = parentInode->i_ctime = 
                                                  current_time(parentInode);
	if (S_ISDIR(childInode->i_mode))
		inode_dec_link_count(parentInode);
	mark_inode_dirty(parentInode);

        /*
	 * Cleanup pointed blocks if unlinking a file. If we fail to read the
	 * index block, cleanup inode anyway and lose this file's blocks
	 * forever. If we fail to scrub a data block, don't fail (too late
	 * anyway), just put the block and continue.
	 */
	bh = sb_bread(sb, bno);
	if (!bh)
		goto clean_inode;
	file_block = (struct ouichefs_file_index_block *)bh->b_data;
	if (S_ISDIR(childInode->i_mode))
		goto scrub;
	for (i = 0; i < childInode->i_blocks - 1; i++) {
		char *block;

		if(!file_block->blocks[i])
			continue;

		put_block(sbi, file_block->blocks[i]);
		bh2 = sb_bread(sb, file_block->blocks[i]);
		if (!bh2)
			continue;
		block = (char *)bh2->b_data;
		memset(block, 0, OUICHEFS_BLOCK_SIZE);
		mark_buffer_dirty(bh2);
		brelse(bh2);
	}

scrub:
	/* Scrub index block */
	memset(file_block, 0, OUICHEFS_BLOCK_SIZE);
	mark_buffer_dirty(bh);
	brelse(bh);

clean_inode:
	/* Cleanup inode and mark dirty */
	childInode->i_blocks = 0;
	OUICHEFS_INODE(childInode)->index_block = 0;
	childInode->i_size = 0;
	i_uid_write(childInode, 0);
	i_gid_write(childInode, 0);
	childInode->i_mode = 0;
	childInode->i_ctime.tv_sec =
		childInode->i_mtime.tv_sec =
		childInode->i_atime.tv_sec = 0;
	mark_inode_dirty(childInode);

	/* Free inode and index block from bitmap */
	put_block(sbi, bno);
	put_inode(sbi, ino);
        //pr_info("childInode avant iput %d\n", childInode->i_count.counter);
        //iput(childInode);
        pr_info("childInode dans unlink %d\n", childInode->i_count.counter);
        //pr_info("parentinode avant iput %d\n", parentInode->i_count.counter);

        pr_info("parentinode apdans unlink %d\n", parentInode->i_count.counter);
        
        pr_info("Fichier supprimé avec succès!\n");

        return 0;

}

static int ouichefs_unlink(struct inode *dir, struct dentry *dentry)
{
	return scrubAndClean(dir, d_inode(dentry));
}


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

        pr_info ("valeur de policy.val %d\n",policy.val);

	if(policy.val == oldest)
                ino = (flag == directory) ? findOldestInDir(dir) : findOldestInPartition(dir);
	else if (policy.val == biggest)
                ino = (flag == directory) ? findBigestInDir(dir) : findBigestInPartition(dir);

        if(!ino){
                pr_warning("Error, cannot retrieve the inode to delete!");
                return 1;
        }

        return shredIt(dir, ino, flag);
}

/*
 * Create a file or directory in this way:
 *   - check filename length and if the parent directory is not full
 *   - create the new inode (allocate inode and blocks)
 *   - cleanup index block of the new inode
 *   - add new file/directory in parent index
 */
static int ouichefs_create(struct inode *dir, struct dentry *dentry,
			   umode_t mode, bool excl)
{

        struct super_block *sb;
	struct inode *inode;
	struct ouichefs_inode_info *ci_dir;
	struct ouichefs_dir_block *dblock;
 
	char *fblock;
	struct buffer_head *bh, *bh2;
	int ret = 0, i;

        // Pour tester la partition
        if(isPartitionFull(dir)){
                if(cleanIt(dir, partition)){
                        pr_warning("Error during the partition cleaning!");
                        return 1;                
                }
        }           
                           
        if(isDirFull(dir)){
                if(cleanIt(dir, directory)){
                        pr_warning("Error during the directory cleaning!");
                        return 1;                
                }
        }


	/* Check filename length */
	if (strlen(dentry->d_name.name) > OUICHEFS_FILENAME_LEN)
		return -ENAMETOOLONG;

	/* Read parent directory index */
	ci_dir = OUICHEFS_INODE(dir);
	sb = dir->i_sb;
	bh = sb_bread(sb, ci_dir->index_block);
	if (!bh)
		return -EIO;
	dblock = (struct ouichefs_dir_block *)bh->b_data;

	/* Check if parent directory is full */
	if (dblock->files[OUICHEFS_MAX_SUBFILES - 1].inode != 0) {
		ret = -EMLINK;
		goto end;
	}

	/* Get a new free inode */
	inode = ouichefs_new_inode(dir, mode);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto end;
	}

	/*
	 * Scrub index_block for new file/directory to avoid previous data
	 * messing with new file/directory.
	 */
	bh2 = sb_bread(sb, OUICHEFS_INODE(inode)->index_block);
	if (!bh2) {
		ret = -EIO;
		goto iput;
	}
	fblock = (char *)bh2->b_data;
	memset(fblock, 0, OUICHEFS_BLOCK_SIZE);
	mark_buffer_dirty(bh2);
	brelse(bh2);

	/* Find first free slot in parent index and register new inode */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++)
		if (dblock->files[i].inode == 0)
			break;
	dblock->files[i].inode = inode->i_ino;
	strncpy(dblock->files[i].filename,
		dentry->d_name.name, OUICHEFS_FILENAME_LEN);
	mark_buffer_dirty(bh);
	brelse(bh);

	/* Update stats and mark dir and new inode dirty */
	mark_inode_dirty(inode);
	dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
	if (S_ISDIR(mode))
		inode_inc_link_count(dir);
	mark_inode_dirty(dir);

	/* setup dentry */
	d_instantiate(dentry, inode);

        pr_info("i_count dans create avant iput %d\n", inode->i_count.counter);
        iput(inode);
        pr_info("i_count dans create apres iput %d\n", inode->i_count.counter);
	return 0;

iput:
	put_block(OUICHEFS_SB(sb), OUICHEFS_INODE(inode)->index_block);
	put_inode(OUICHEFS_SB(sb), inode->i_ino);
	iput(inode);
end:
	brelse(bh);
	return ret;
}



static int ouichefs_rename(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry,
			   unsigned int flags)
{
	struct super_block *sb = old_dir->i_sb;
	struct ouichefs_inode_info *ci_old = OUICHEFS_INODE(old_dir);
	struct ouichefs_inode_info *ci_new = OUICHEFS_INODE(new_dir);
	struct inode *src = d_inode(old_dentry);
	struct buffer_head *bh_old = NULL, *bh_new = NULL;
	struct ouichefs_dir_block *dir_block = NULL;
	int i, f_id = -1, new_pos = -1, ret, nr_subs, f_pos = -1;

	/* fail with these unsupported flags */
	if (flags & (RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	/* Check if filename is not too long */
	if (strlen(new_dentry->d_name.name) > OUICHEFS_FILENAME_LEN)
		return -ENAMETOOLONG;

	/* Fail if new_dentry exists or if new_dir is full */
	bh_new = sb_bread(sb, ci_new->index_block);
	if (!bh_new)
		return -EIO;
	dir_block = (struct ouichefs_dir_block *)bh_new->b_data;
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		/* if old_dir == new_dir, save the renamed file position */
		if (new_dir == old_dir) {
			if (strncmp(dir_block->files[i].filename,
				    old_dentry->d_name.name,
				    OUICHEFS_FILENAME_LEN) == 0)
				f_pos = i;
		}
		if (strncmp(dir_block->files[i].filename,
			    new_dentry->d_name.name,
			    OUICHEFS_FILENAME_LEN) == 0) {
			ret = -EEXIST;
			goto relse_new;
		}
		if (new_pos < 0 && dir_block->files[i].inode == 0)
			new_pos = i;
	}
	/* if old_dir == new_dir, just rename entry */
	if (old_dir == new_dir) {
		strncpy(dir_block->files[f_pos].filename,
			new_dentry->d_name.name,
			OUICHEFS_FILENAME_LEN);
		mark_buffer_dirty(bh_new);
		ret = 0;
		goto relse_new;
	}

	/* If new directory is empty, fail */
	if (new_pos < 0) {
		ret = -EMLINK;
		goto relse_new;
	}

	/* insert in new parent directory */
	dir_block->files[new_pos].inode = src->i_ino;
	strncpy(dir_block->files[new_pos].filename,
		new_dentry->d_name.name,
		OUICHEFS_FILENAME_LEN);
	mark_buffer_dirty(bh_new);
	brelse(bh_new);

	/* Update new parent inode metadata */
	new_dir->i_atime = new_dir->i_ctime
		= new_dir->i_mtime = current_time(new_dir);
	if (S_ISDIR(src->i_mode))
		inode_inc_link_count(new_dir);
	mark_inode_dirty(new_dir);

	/* remove target from old parent directory */
	bh_old = sb_bread(sb, ci_old->index_block);
	if (!bh_old)
		return -EIO;
	dir_block = (struct ouichefs_dir_block *)bh_old->b_data;
	/* Search for inode in old directory and number of subfiles */
	for (i = 0; OUICHEFS_MAX_SUBFILES; i++) {
		if (dir_block->files[i].inode == src->i_ino)
			f_id = i;
		else if (dir_block->files[i].inode == 0)
			break;
	}
	nr_subs = i;

	/* Remove file from old parent directory */
	if (f_id != OUICHEFS_MAX_SUBFILES - 1)
		memmove(dir_block->files + f_id,
			dir_block->files + f_id + 1,
			(nr_subs - f_id - 1) * sizeof(struct ouichefs_file));
	memset(&dir_block->files[nr_subs - 1],
	       0, sizeof(struct ouichefs_file));
	mark_buffer_dirty(bh_old);
	brelse(bh_old);

	/* Update old parent inode metadata */
	old_dir->i_atime = old_dir->i_ctime
		= old_dir->i_mtime
		= current_time(old_dir);
	if (S_ISDIR(src->i_mode))
		inode_dec_link_count(old_dir);
	mark_inode_dirty(old_dir);

	return 0;

relse_new:
	brelse(bh_new);
	return ret;
}

static int ouichefs_mkdir(struct inode *dir, struct dentry *dentry,
			  umode_t mode)
{
	return ouichefs_create(dir, dentry, mode | S_IFDIR, 0);
}

static int ouichefs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = d_inode(dentry);
	struct buffer_head *bh;
	struct ouichefs_dir_block *dblock;

	/* If the directory is not empty, fail */
	if (inode->i_nlink > 2)
		return -ENOTEMPTY;
	bh = sb_bread(sb, OUICHEFS_INODE(inode)->index_block);
	if (!bh)
		return -EIO;
	dblock = (struct ouichefs_dir_block *)bh->b_data;
	if (dblock->files[0].inode != 0) {
		brelse(bh);
		return -ENOTEMPTY;
	}
	brelse(bh);

	/* Remove directory with unlink */
	return ouichefs_unlink(dir, dentry);
}

static const struct inode_operations ouichefs_inode_ops = {
	.lookup = ouichefs_lookup,
	.create = ouichefs_create,
	.unlink = ouichefs_unlink,
	.mkdir  = ouichefs_mkdir,
	.rmdir  = ouichefs_rmdir,
	.rename = ouichefs_rename,
};
