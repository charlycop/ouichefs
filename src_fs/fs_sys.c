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
//#include "../src_mod/policy/policy.h"

// Allows to choose the cleaning policy with another module
TypePolicy policy;
EXPORT_SYMBOL(policy);

/* will be use for the syscall */
static struct dentry *dentry_root;

/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
	struct dentry *dentry = NULL;
	policy = tp_oldest;

	dentry = mount_bdev(fs_type, flags, dev_name, data,
			    ouichefs_fill_super);
	if (IS_ERR(dentry))
		pr_err("'%s' mount failure\n", dev_name);
	else
		pr_info("'%s' mount success\n", dev_name);

	dentry_root = dentry;

	return dentry;
}

/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
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

// Syscall ouichefs
extern long (*module_clear_ouichefs)(int);

/*
 * Syscall, allows to clean the partition from user space
 * syscall_policy : delete the biggest or the oldest inode.
 * return 0 if success, anything else otherwise.
*/
long custom_syscall(int syscall_policy)
{
	struct inode *ino = NULL;
	TypePolicy old_policy = policy;
	int res;

	/* modify the policy */
	if (syscall_policy == tp_oldest || syscall_policy == tp_biggest)
		policy = syscall_policy;

	pr_info("Executing clear_ouichefs system call\n");
	pr_info("Policy = %d\n", policy);

	ino = ouichefs_iget(dentry_root->d_sb, 0);

	/* launch the cleaning on the whole partition */
	res = clean_it(ino, tp_partition);
	if (res != 0)
		pr_info("Error in clean_it() called from syscall\n");

	/* put back the original policy */
	policy = old_policy;

	return res;
}

static int __init ouichefs_init(void)
{
	int ret;

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

	module_clear_ouichefs = &(custom_syscall);

	if (module_clear_ouichefs == NULL)
		pr_info("Syscall not wrapped\n");

	pr_info("module loaded\n");

	end:
	return ret;
}

static void __exit ouichefs_exit(void)
{
	int ret;
	// restore syscall to Null when unloading module
	module_clear_ouichefs = NULL;


	ret = unregister_filesystem(&ouichefs_file_system_type);
	if (ret)
		pr_err("unregister_filesystem() failed\n");

	ouichefs_destroy_inode_cache();

	pr_info("module unloaded\n");
}

module_init(ouichefs_init);
module_exit(ouichefs_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Redha Gouicem, <redha.gouicem@lip6.fr>");
MODULE_DESCRIPTION("ouichefs, a simple educational filesystem for Linux");
