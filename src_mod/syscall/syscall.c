
// Syscall Stealing
#define MAX_LEN_ENTRY   256

static unsigned long *sys_call_table;
static unsigned long orig_cr0;

asmlinkage long (*original_clear) (void);

asmlinkage long our_sys_open(void)
{
	pr_info("Syscall volé, on peut maintenant appeler shred_it()\n");
	return 0;
}

/*
unsigned long *find_syscall_table(void)
{
	unsigned long *syscall_table;
	unsigned long int i;

	sys_io_setup(1, 100);

	for (i = (unsigned long int)sys_io_setup; i < ULONG_MAX; i += sizeof(void *)) {
		syscall_table = (unsigned long *)i;

		if (syscall_table[__NR_io_setup] == (unsigned long)sys_io_setup) {
			pr_info("sys_io_setup = %lx\n", syscall_table[__NR_io_setup]);
			return syscall_table;
		}
	}
	return NULL;
}
*/

void hook_syscall(void) {
	sys_call_table = kmalloc(sizeof(unsigned long), GFP_KERNEL); // sinon NULL deref pointer
	sys_call_table = (unsigned long*)kallsyms_lookup_name("sys_call_table");
	pr_info("The address of sys_call_table is: %lx\n", sys_call_table[0]);
	pr_info("NR_OUICHEFS = %d\n", __NR_clear_ouichefs);

	original_clear = (void*)sys_call_table[__NR_clear_ouichefs];

	pr_info("Address syscall[548] avant ecriture: %lx\n", sys_call_table[__NR_clear_ouichefs]);

	orig_cr0 =  read_cr0();
	write_cr0(orig_cr0 & (~ 0x10000)); /* Set WP flag to 0 */
	sys_call_table[__NR_clear_ouichefs]  = (unsigned long)our_sys_open;
	write_cr0(orig_cr0); /* Set WP flag to 1 */

	pr_info("Address syscall[548] après ecriture: %lx\n", sys_call_table[__NR_clear_ouichefs]);
}

void restore_syscall(void) {
	orig_cr0 =  read_cr0();
	write_cr0(orig_cr0 & (~ 0x10000)); /* Set WP flag to 0 */
	sys_call_table[548] = (unsigned long)original_clear;
	write_cr0(orig_cr0); /* Set WP flag to 1 */
}

