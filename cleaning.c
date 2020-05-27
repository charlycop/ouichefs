#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>

#include "ouichefs.h"
#include "bitmap.h"
#include "cleaning.h" // Partition cleaning tools and policies

static const struct inode_operations ouichefs_inode_ops;

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

        pr_info("Fichier supprimé avec succès!\n");

        return 0;

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
                        
                        if(res > 0)
                                return res;
                }
                else if (ino == inoToFind){
                        pr_info("fichier : %s avec ino:%lu localisé!\n",
                                            dir_block->files[i].filename, ino);
                        return inoParent; // on renvoie le parent
                }
                ++i;       
	}

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
                
                // si pas fichier ou utilisé
                if (S_ISDIR(inode->i_mode) || inode->i_count.counter)
                        continue;
                
                if(inode->i_mtime.tv_sec < min){
                        min = inode->i_mtime.tv_sec;
                        ino_ancien = ino;
                }
                ++i;       
	}

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
                        if (S_ISDIR(inode->i_mode) || inode->i_count.counter)
                                continue;

                        if(inode->i_mtime.tv_sec < min){
                                min = inode->i_mtime.tv_sec;
                                ino_ancien = ino;
                        }
                }
        }

        pr_info("Le plus ancien de la partition est l'ino #%lu\n", ino_ancien);

        return ino_ancien;
}

/*
 * Free the dir of the inode and free the blocks of the inode
 * dir : current directory of the file
 * flag : 0 search for parent, 1 parent is dir
 */
ssize_t shredIt(struct inode *dir, unsigned long ino, uint8_t flag){
      
        struct super_block *sb = dir->i_sb;
        struct inode *inodeToDelete = NULL, *inodeParent = dir;
        struct dentry *dentry = NULL;
        unsigned long parent = 0;

        inodeToDelete = ouichefs_iget(sb, ino);     

        if(inodeToDelete->i_dentry.first != NULL){ // si dentry : on fait unlink direct
                dentry = hlist_entry(inodeToDelete->i_dentry.first, 
                                           struct dentry, d_u.d_alias);
                // DILEMME UTILISE-T-ON UNLINK pour montrer qu'on a bien capter la diff' ?
                return ouichefs_unlink(dentry->d_parent->d_inode, dentry);
        }
        else{  
                if(!flag){
                        parent = findParentOfIno(dir, 0, ino);
                        inodeParent = ouichefs_iget(sb, parent);
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
ssize_t cleanIt(struct inode *dir, uint8_t flag)
{
        unsigned long ino = (flag) ? 
                             findOldestInDir(dir) : findOldestInPartition(dir);
                      
        if(!ino){
                pr_warning("Error, cannot retrieve the oldest Inode!");
                return 1;
        }

        return shredIt(dir, ino, flag);
}
