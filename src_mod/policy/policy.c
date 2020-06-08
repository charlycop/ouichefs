#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "policy.h"

extern TypePolicy policy;

MODULE_DESCRIPTION("Module for policy modification in OUICHEFS");
MODULE_AUTHOR("Hugo Lopes, Pierre Boeshertz, Charly Oudy, SESI M1 2020");
MODULE_LICENSE("GPL");

static int __init hello_init(void)
{
	policy = tp_biggest;
	pr_info("Changing cleaning policy : BIGGEST FILE\n");
	return 0;
}
module_init(hello_init);

static void __exit hello_exit(void)
{
	policy = tp_oldest;
	pr_info("Changing cleaning policy : OLDEST FILE\n");
}
module_exit(hello_exit);
