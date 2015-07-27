/* See COPYRIGHT for copyright information. */

#include <mmu.h>
#include <error.h>
#include <env.h>
#include <kerelf.h>
#include <sched.h>
#include <pmap.h>
#include <printf.h>

struct Env *envs = NULL;		// All environments
struct Env *curenv = NULL;	        // the current env

static struct Env_list env_free_list;	// Free list

extern Pde *boot_pgdir;
//static int KERNEL_SP;
extern char *KERNEL_SP;
//
// Calculates the envid for env e.
//
u_int mkenvid(struct Env *e)
{
	static u_long next_env_id = 0;

	// lower bits of envid hold e's position in the envs array
	u_int idx = e - envs;

	//printf("env.c:mkenvid:\txx:%x\n",(int)&idx);

	// high bits of envid hold an increasing number
	return (++next_env_id << (1 + LOG2NENV)) | idx;
}

//
// Converts an envid to an env pointer.
//
// RETURNS
//   env pointer -- on success and sets *error = 0
//   NULL -- on failure, and sets *error = the error number
//
int envid2env(u_int envid, struct Env **penv, int checkperm)
{
	struct Env *e;
	u_int cur_envid;

	if (envid == 0) {
		*penv = curenv;
		return 0;
	}

	e = &envs[ENVX(envid)];

	//printf("envid:%x  ENVX(envid):%x  AAAA:%d  BBBB:%d CCCC:%x\n",envid,ENVX(envid),(int)(e->env_status == ENV_FREE),(int)(e->env_id ),&(e->env_id ));
	if (e->env_status == ENV_FREE || e->env_id != envid) {
		*penv = 0;
		return -E_BAD_ENV;
	}

	if (checkperm) {
		// Your code here in Lab 4
		cur_envid = envid;

		while (&envs[ENVX(cur_envid)] != curenv && ENVX(cur_envid) != 0) {
			envid = envs[ENVX(cur_envid)].env_parent_id;
			cur_envid = envid;
		}

		//panic("AAAA:%x  BBBB:%x",(int)(&envs[ENVX(cur_envid)] != curenv),(int)(ENVX(cur_envid) != 0));
		if (ENVX(cur_envid) == 0) {
			*penv = 0;
			//*error = -E_BAD_ENV;
			return -E_BAD_ENV;
		}
	}

	*penv = e;
	return 0;
}

//
// Marks all environments in 'envs' as free and inserts them into
// the env_free_list.  Insert in reverse order, so that
// the first call to env_alloc() returns envs[0].
//
void
env_init(void)
{
	int i;

	printf("env.c:\tenvs:\tcon:%x\n", (int)envs);

	LIST_INIT(&env_free_list);

	for (i = NENV - 1; i >= 0; i--) {
		envs[i].env_status = ENV_FREE;
		LIST_INSERT_HEAD(&env_free_list, &envs[i], env_link);
	}

	printf("env.c:\tenv_init success!\n");
}

//
// Initializes the kernel virtual memory layout for environment e.
//
// Allocates a page directory and initializes it.  Sets
// e->env_cr3 and e->env_pgdir accordingly.
//
// RETURNS
//   0 -- on sucess
//   <0 -- otherwise
//
static int
env_setup_vm(struct Env *e)
{
	// Hint:

	int i, r;
	struct Page *p = NULL;

	Pde *pgdir;

	// Allocate a page for the page directory
	if ((r = page_alloc(&p)) < 0) {
		panic("env_setup_vm - page_alloc error\n");
		return r;
	}

	p->pp_ref++;
	//printf("env.c:env_setup_vm:page_alloc:p\t@page:%x\t@:%x\tcon:%x\n",page2pa(p),(int)&p,(int)p);
	//printf("env_setup_vm :	1\n");
	// Hint:
	//    - The VA space of all envs is identical above UTOP
	//      (except at VPT and UVPT)
	//    - Use boot_pgdir
	//    - Do not make any calls to page_alloc
	//    - Note: pp_refcnt is not maintained for physical pages mapped above UTOP.
	pgdir = (Pde *)page2kva(p);

	//	printf("env.c:env_setup_vm:\tpgdir\t:con:%x\n",(int)pgdir);
	for (i = 0; i < UTOP; i += BY2PG) {
		pgdir[PDX(i)] = 0;
	}

	for (i = PDX(UTOP); i < 1024; i++) {
		//printf("boot_pgdir[%d] = %x\n",i,boot_pgdir[PDX(i)]);
		pgdir[i] = boot_pgdir[i];
	}

	//printf("env_setup_vm :	2\n");

	e->env_pgdir = pgdir;
	//printf("env_setup_vm :	3\n");
	// ...except at VPT and UVPT.  These map the env's own page table

	//e->env_pgdir[PDX(UVPT)]  = e->env_cr3 | PTE_P | PTE_U;

	e->env_cr3 = PADDR(pgdir);

	boot_map_segment(e->env_pgdir, UVPT, PDMAP, PADDR(pgdir), PTE_R);
	//	printf("env.c:env_setup_vm:\tboot_map_segment(%x,%x,%x,%x,PTE_R)\n",e->env_pgdir,UVPT,PDMAP,PADDR(pgdir));
	e->env_pgdir[PDX(UVPT)]  = e->env_cr3 | PTE_V | PTE_R;
	//printf("env_setup_vm :  4\n");
	return 0;
}

//
// Allocates and initializes a new env.
//
// RETURNS
//   0 -- on success, sets *new to point at the new env
//   <0 -- on failure
//
int
env_alloc(struct Env **new, u_int parent_id)
{
	int r;
	struct Env *e;

	if (!(e = LIST_FIRST(&env_free_list))) {
		return -E_NO_FREE_ENV;
	}

	//printf("come 0\n");
	if ((r = env_setup_vm(e)) < 0) {
		return r;
	}

	//printf("come 1\n");
	e->env_id = mkenvid(e);
	e->env_parent_id = parent_id;
	e->env_status = ENV_RUNNABLE;
	//printf("come 2\n");
	// Set initial values of registers
	//  (lower 2 bits of the seg regs is the RPL -- 3 means user process)
	e->env_tf.regs[29] = USTACKTOP;
	e->env_tf.cp0_status = 0x10001004;
	// You also need to set tf_eip to the correct value.
	// Hint: see load_icode
	/*
		e->env_ipc_blocked = 0;
		e->env_ipc_value = 0;
		e->env_ipc_from = 0;
	*/
	e->env_ipc_recving = 0;
	e->env_pgfault_handler = 0;
	e->env_xstacktop = 0;
	//printf("come 2\n");
	// commit the allocation
	LIST_REMOVE(e, env_link);
	*new = e;
	//printf("should be there?\n");
	//	printf("env.c:env_alloc:\tnew env:%x\t(((envid)>> 11)<<6)@:%x\n",e->env_id,(int)e);
	return 0;
}

/* Overview:
 *   This is a call back function for kernel's elf loader.
 * Elf loader extracts each segment of the given binary image.
 * Then the loader calls this function to map each segment
 * at correct virtual address.
 *
 *   `bin_size` is the size of `bin`. `sgsize` is the
 * segment size in memory.
 *
 * Pre-Condition:
 *   va aligned 4KB and bin can't be NULL.
 *
 * Post-Condition:
 *   return 0 on success, otherwise < 0.
 */
static int load_icode_mapper(u_long va, u_int32_t sgsize,
							 u_char *bin, u_int32_t bin_size, void *user_data)
{
	struct Env *env = (struct Env *)user_data;
	struct Page *p = NULL;
	u_long i;
	int r;

	// Step 1: load all content of bin into memory.
	for (i = 0; i < bin_size; i += BY2PG) {
		// Hint: You should alloc a page and increase the reference count
		//       of it.
		if ((r = page_alloc(&p)) < 0) {
			return r;
		}

		p->pp_ref++;

		if (bin_size - i >= BY2PG) {
			bcopy(bin + i, (void *)page2kva(p), BY2PG);
		} else {
			bcopy(bin + i, (void *)page2kva(p), bin_size - i);
		}

		// FIXME: some pages should be read-only.
		page_insert(env->env_pgdir, p, va + i, PTE_V | PTE_R);
	}

	// Step 2: alloc pages to reach `sgsize` when `sgsize` < `bin_size`
	while (i < sgsize) {
		if ((r = page_alloc(&p)) < 0) {
			return r;
		}

		p->pp_ref++;

		// FIXME: some pages should be read-only.
		page_insert(env->env_pgdir, p, va + i, PTE_V | PTE_R);

		i += BY2PG;
	}

	return 0;
}
/* Overview:
 *   Sets up the the initial stack and program binary for a user process.
 *
 *   This function loads the complete binary image by using elf loader,
 * into the environment's user memory. The entry point of the binary image
 * is given by the elf loader. And this function maps one page for the
 * program's initial stack at virtual address USTACKTOP - BY2PG.
 *
 * Note: All mappings are read/write including those of the text segment.
 */
static void
load_icode(struct Env *e, u_char *binary, u_int size)
{
	// Hint:
	//  Use page_alloc, page_insert, page2kva and e->env_pgdir
	//  You must figure out which permissions you'll need
	//  for the different mappings you create.
	//  Remember that the binary image is an a.out format image,
	//  which contains both text and data.
	struct Page *p = NULL;
	u_long entry_point;
	u_long r;

	if ((r = page_alloc(&p)) < 0) {
		panic ("page alloc error!");
	}

	p->pp_ref++;
	//cgh:create initial stack for the user process e
	u_long perm;
	perm = PTE_R | PTE_V;

	page_insert(e->env_pgdir, p, USTACKTOP - BY2PG, perm);

	//load the binary by using elf loader
	load_elf(binary, size, &entry_point, e, load_icode_mapper);

	//^e->env_tf.pc=UTEXT+(index[6] & 0x000FFFFF);
	e->env_tf.pc = entry_point;
	//	printf("#env.c:\tloadicode():\tthe text is begin @0x%x\tcr3:%x\n",e->env_tf.pc,e->env_cr3);
}

//
// Allocates a new env and loads the a.out binary into it.
//  - new env's parent env id is 0
void
env_create(u_char *binary, int size)
{
	struct Env *e;
	//	printf("be going to env_alloc() in env_create()\n");
	env_alloc(&e, 0);

	load_icode(e, binary, size);
	//panic("--------------------------------------");
}


//
// Frees env e and all memory it uses.
//
void
env_free(struct Env *e)
{
	Pte *pt;
	u_int pdeno, pteno, pa;

	// Note the environment's demise.
	printf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

	// Flush all pages

	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {
		if (!(e->env_pgdir[pdeno] & PTE_V)) {
			continue;
		}

		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (Pte *)KADDR(pa);

		for (pteno = 0; pteno <= PTX(~0); pteno++)
			if (pt[pteno] & PTE_V) {
				page_remove(e->env_pgdir, (pdeno << PDSHIFT) | (pteno << PGSHIFT));
			}

		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa));
	}

	pa = e->env_cr3;
	e->env_pgdir = 0;
	e->env_cr3 = 0;
	page_decref(pa2page(pa));


	e->env_status = ENV_FREE;
	LIST_INSERT_HEAD(&env_free_list, e, env_link);
}

// Frees env e.  And schedules a new env
// if e is the current env.
//
void
env_destroy(struct Env *e)
{
	env_free(e);

	if (curenv == e) {
		curenv = NULL;
		//panic("bcopy(): src:%x  dst:%x ",(int)KERNEL_SP-sizeof(struct Trapframe),TIMESTACK-sizeof(struct Trapframe));
		bcopy((void *)KERNEL_SP - sizeof(struct Trapframe),
			  (void *)TIMESTACK - sizeof(struct Trapframe),
			  sizeof(struct Trapframe));
		printf("i am killed ... \n");
		sched_yield();
	}
}


//
// Restores the register values in the Trapframe
//  (does not return)
//
extern void env_pop_tf(struct Trapframe *tf, int id);
extern void lcontext(u_int contxt);

//
// Context switch from curenv to env e.
// Note: if this is the first call to env_run, curenv is NULL.
//  (This function does not return.)
//
void
env_run(struct Env *e)
{
	// step 1: save register state of curenv
	struct Trapframe  *old;
	old = (struct Trapframe *)(TIMESTACK - sizeof(struct Trapframe));

	if (curenv) {
		bcopy(old, &curenv->env_tf, sizeof(struct Trapframe));

		//panic("^^^src@:%x\tdst@:%x\tsize:%x",(int)old,(int)&curenv->env_tf,sizeof(struct Trapframe));
		curenv->env_tf.pc = old->cp0_epc;

		//panic("-----------------------------------------------");
	}

	//panic("**************************************");
	// step 2: set curenv
	curenv = e;
	// step 3: use lcr3
	//printf("._%x:_.",(int)curenv->env_id);
	//printf("\nenv_run():id %x\tpc:%x\tcurenv:\tsp:0x%x\t*:0x%x\n",(int)curenv->env_id,(int)curenv->env_tf.pc,(int)curenv->env_tf.regs[29],(int)curenv->env_cr3);


	lcontext(KADDR(curenv->env_cr3));
	// step 4: use env_pop_tf()

	//if (curenv->env_id==0x10d8)
	//{
	//	for(;;);
	//}

	env_pop_tf(&(curenv->env_tf), GET_ENV_ASID(curenv->env_id));

	panic("there is never reached otherwise error !\n");
	// Hint: Skip step 1 until exercise 4.  You don't
	// need it for exercise 1, and in exercise 4 you'll better
	// understand what you need to do.
}

