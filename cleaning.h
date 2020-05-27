#ifndef _CLEANING_H
#define _CLEANING_H


int scrubAndClean(struct inode *parentInode, struct inode *childInode);

int ouichefs_unlink(struct inode *dir, struct dentry *dentry);

/*
 * Calculate the usage % of the partition
 * dir : current directory inode
 * and return 1 if it is too much, 0 if everything is under control
 */
ssize_t isPartitionFull(struct inode *dir);


/*
 * Check if the OUICHEFS_MAX_SUBFILES is reached.
 * dir : current directory inode
 * and return 1 if it is reached, 0 is not.
 */
ssize_t isDirFull(struct inode *dir);

unsigned long findParentOfIno(struct inode *dir, unsigned long inoParent,
                                unsigned long inoToFind);

unsigned long findOldestInDir(struct inode *dir);


unsigned long findOldestInPartition(struct inode *dir);


/*
 * Free the dir of the inode and free the blocks of the inode
 * dir : current directory of the file
 * flag : 0 search for parent, 1 parent is dir
 */
ssize_t shredIt(struct inode *dir, unsigned long ino, uint8_t flag);


/*
 * Free the partition or the dir of the oldest file.
 * dir : current directory
 * flag : 0 for partition, 1 for dir
 */
ssize_t cleanIt(struct inode *dir, uint8_t flag);



#endif	/* _CLEANING_H */
