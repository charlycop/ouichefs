#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "size.h"

extern policies policy;


MODULE_DESCRIPTION("Module \"hello word\" pour noyau linux");
MODULE_AUTHOR("Julien Sopena, LIP6");
MODULE_LICENSE("GPL");

static int __init hello_init(void)
{
	policy.val = 1;
	pr_info("changement de politique de netoyage: %d\n",policy.val);
	return 0;
}
module_init(hello_init);

static void __exit hello_exit(void)
{
	policy.val = 0;
	pr_info("Policy back to %d\n", policy.val);
}
module_exit(hello_exit);
