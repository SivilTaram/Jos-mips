// implement fork from user space

#include "lib.h"
#include <mmu.h>
#include <env.h>

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//

void user_bcopy(const void *src, void *dst, size_t len)
{
	void *max;

	//	writef("~~~~~~~~~~~~~~~~ src:%x dst:%x len:%x\n",(int)src,(int)dst,len);
	max = dst + len;

	// copy machine words while possible
	if (((int)src % 4 == 0) && ((int)dst % 4 == 0)) {
		while (dst + 3 < max) {
			*(int *)dst = *(int *)src;
			dst += 4;
			src += 4;
		}
	}

	// finish remaining 0-3 bytes
	while (dst < max) {
		*(char *)dst = *(char *)src;
		dst += 1;
		src += 1;
	}

	//for(;;);
}


void user_bzero(void *v, u_int n)
{
	char *p;
	int m;

	p = v;
	m = n;

	while (--m >= 0) {
		*p++ = 0;
	}
}

static void
pgfault(u_int va)
{
	u_int *tmp;
	//	writef("fork.c:pgfault():\t va:%x\n",va);

	if (((*vpt)[VPN(va)] & PTE_COW) == 0) {
		user_panic("It is not a copy-on-write page at va:%d\n", va);
	}

	syscall_mem_alloc(0, BY2PG,
					  PTE_V | PTE_R);	//map the new page at a temporary place

	//copy the content
	tmp = (u_int *)ROUNDDOWN(va, BY2PG);
	user_bcopy((void *)tmp, (void *)BY2PG, BY2PG);
	//map the page on the appropriate place
	syscall_mem_map(0, BY2PG, 0, va, PTE_V | PTE_R);
	// Your code here.
	//	writef("fork.c:pgfault(): It's END!\n");
}

//
// Map our virtual page pn (address pn*BY2PG) into the target envid
// at the same virtual address.  if the page is writable or copy-on-write,
// the new mapping must be created copy on write and then our mapping must be
// marked copy on write as well.  (Exercise: why mark ours copy-on-write again if
// it was already copy-on-write?)
//
static void
duppage(u_int envid, u_int pn)
{
	u_int addr;
	u_int perm;

	perm = (*vpt)[pn] & 0xfff;
	addr = pn * BY2PG;

	if ((perm & PTE_R) != 0 || (perm & PTE_COW) != 0) {
		if (perm & PTE_LIBRARY) {
			perm = PTE_V | PTE_R | PTE_LIBRARY;
		} else {
			perm = PTE_V | PTE_COW;
		}

		syscall_mem_map(0, addr, envid, addr, perm);
		syscall_mem_map(0, addr, 0, addr, perm);
	} else {
		//user_panic("_______page is not write & COW______");
		syscall_mem_map(0, addr, envid, addr, perm);
	}

	return;
	//	user_panic("duppage not implemented");
}

//
// User-level fork.  Create a child and then copy our address space
// and page fault handler setup to the child.
//
// Hint: use vpd, vpt, and duppage.
// Hint: remember to fix "env" in the child process!
//
extern void __asm_pgfault_handler(void);
int
fork(void)
{
	// Your code here.
	u_int newenvid;
	extern struct Env *envs;
	extern struct Env *env;
	u_int i;

	//	writef("fork.c:\tfork is begin!\n");

	//The parent installs pgfault using set_pgfault_handler
	set_pgfault_handler(pgfault);

	//alloc a new alloc
	if ((newenvid = syscall_env_alloc()) < 0) {
		writef("fork:no env can be alloced\n");
		return newenvid;
	}

	//writef("newenvid = %x\n",newenvid);
	if (newenvid == 0) {
		//		writef("@@@@@@@ I'm child @@@@@@@\n");
		env = &envs[ENVX(syscall_getenvid())];
		return 0;
	}

	//	writef("fork.c:\tvpd:%x\tvpt:%x\n",(int)vpd,(int)vpt);
	for (i = 0; i < (UTOP / BY2PG) - 1; i++) {

		if (((*vpd)[i / PTE2PT]) != 0 && ((*vpt)[i]) != 0) {
			//			writef("i:%x\tvpd[i/PTE2PT]:%x\tvpt[i]:%x\n",i,(*vpd)[i/PTE2PT],(*vpt)[i]);
			duppage(newenvid, i);
		}
	}

	//writef("newenvdi:%x\n",newenvid);
	if (syscall_mem_alloc(newenvid, UXSTACKTOP - BY2PG, PTE_V | PTE_R) < 0) {
		writef("syscall_mem_alloc for UXSTACK is wrong\n");
	}

	//writef("end of alloc UXSTACKTOP\n");
	//	writef("________________handler %x\n",  __asm_pgfault_handler);
	syscall_set_pgfault_handler(newenvid, __asm_pgfault_handler, UXSTACKTOP);
	//writef("end of set pgfault handler\n");
	syscall_set_env_status(newenvid, ENV_RUNNABLE);
	//syscall_set_env_status(0x800, ENV_NOT_RUNNABLE);
	//writef("end of set child env runnable\n");
	//	tlbflush();
	//user_panic("_________________________");

	return newenvid;
	//	user_panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	user_panic("sfork not implemented");
	return -E_INVAL;
}
