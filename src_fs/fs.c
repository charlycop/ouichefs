// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018  Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>


#include "ouichefs.h"

/* Allows inject a new cleaning policy with another module */
strategy policy;
EXPORT_SYMBOL(policy);

static struct dentry *root_dentry = NULL;

/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
	struct dentry *dentry = NULL;

	dentry = mount_bdev(fs_type, flags, dev_name, data,
			    ouichefs_fill_super);
	if (IS_ERR(dentry))
		pr_err("'%s' mount failure\n", dev_name);
	else
		pr_info("'%s' mount success\n", dev_name);
        
        root_dentry = dentry;
	return dentry;
}

/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
        struct list_head *pos, *n;

        list_for_each_safe(pos,n, &sb->s_inodes)
                list_del(pos);

        kill_block_super(sb);
	pr_info("unmounted disk\n");
}

static struct file_system_type ouichefs_file_system_type = {
	.owner = THIS_MODULE,
	.name = "ouichefs",
	.mount = ouichefs_mount,
	.kill_sb = ouichefs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV,
	.next = NULL,
};

//----- SYSFS -----
static ssize_t ouichefs_cleaning_store(struct kobject *kobj,
                           struct kobj_attribute *attr,
                           const char *buf, size_t count)
{   
	
	ssize_t res;
	char phrase[30];

	if((res = snprintf(phrase, count+1, buf)) < 0)
        	return -EINVAL;

	if (!strcmp(phrase,"start\n")){
                pr_info("phrase = %s", phrase);
                clean_it(root_dentry->d_inode, tp_partition);
        }

	return res;
}

static struct kobj_attribute cleaning_attribute=__ATTR_WO(ouichefs_cleaning); 
// FIN SYSFS ------

static int __init ouichefs_init(void)
{
	int ret, retval;
        ouichefs_destroy_inode_cache();
	ret = ouichefs_init_inode_cache();
	if (ret) {
		pr_err("inode cache creation failed\n");
		goto end;
	}

	ret = register_filesystem(&ouichefs_file_system_type);
	if (ret) {
		pr_err("register_filesystem() failed\n");
		goto end;
	}

        retval = sysfs_create_file(kernel_kobj, &cleaning_attribute.attr);  

        /* initialize the cleaning strategy (oldest file by default) */
        policy.inDir = oldest_in_dir;
        policy.inPartition = oldest_in_partition;

	pr_info("module loaded\n");
end:
	return ret;
}

static void __exit ouichefs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&ouichefs_file_system_type);
	if (ret)
		pr_err("unregister_filesystem() failed\n");
        
	sysfs_remove_file(kernel_kobj, &cleaning_attribute.attr);

	pr_info("module unloaded\n");
}

module_init(ouichefs_init);
module_exit(ouichefs_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Redha Gouicem, <redha.gouicem@lip6.fr>");
MODULE_DESCRIPTION("ouichefs, a simple educational filesystem for Linux");
