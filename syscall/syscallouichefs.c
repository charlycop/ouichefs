
//// AJOUTEZ CA 
extern long (*module_clear_ouichefs)(void); 

long custom_syscall(void) {
	pr_info("Executing clear_ouichefs system call\n");
	return 0;
}
//////

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

	////// AJOUTEZ CA
	module_clear_ouichefs = &(custom_syscall);
	if(module_clear_ouichefs == NULL) {
		pr_info("Syscall not wrapped\n");
	}

	////////
	pr_info("module loaded\n");
end:
	return ret;
}

static void __exit ouichefs_exit(void)
{
	int ret;

	////// AJOUTEZ CA
	// restore syscall first
	//restore_syscall();
	module_clear_ouichefs = NULL;
	///////

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
