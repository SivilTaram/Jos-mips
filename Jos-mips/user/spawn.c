#include "lib.h"
#include "fd.h"
#include <mmu.h>
#include <env.h>
#include <kerelf.h>

#define TMPPAGE		(BY2PG)
#define TMPPAGETOP	(TMPPAGE+BY2PG)

/* Overview:
 *   This function loads an elf format file frome disk.
 *
 * Post-Condition:
 *   Return the entry point of the file. 0 on error.
 */
u_int load_elf_from_disk(int fd, int child_envid)
{
	Elf32_Ehdr ehdr;
	Elf32_Phdr phdr;
	u_int i;
	void *blk;
	int r;
	Elf32_Half ph_entry_count, ph_entry_size;

	r = readn(fd, (void *)&ehdr, sizeof(Elf32_Ehdr));

	if (r < 0) {
		return 0;
	}

	ph_entry_count = ehdr.e_phnum;
	ph_entry_size = ehdr.e_phentsize;
	seek(fd, ehdr.e_phoff);

	while (ph_entry_count--) {
		r = readn(fd, (void *)&phdr, ph_entry_size);

		if (r < 0) {
			return 0;
		}

		if (phdr.p_type == PT_LOAD) {
			for (i = 0; i < phdr.p_filesz; i += BY2PG) {
				if ((r = read_map(fd, phdr.p_offset + i, &blk)) < 0) {
					writef("mapping text region is wrong\n");
					return r;
				}

				if (phdr.p_filesz - i >= BY2PG) {
					syscall_mem_map(0, (int)blk, child_envid,
									phdr.p_vaddr + i, PTE_V | PTE_R);
				} else {
					syscall_mem_alloc(0, TMPPAGE, PTE_V | PTE_R);
					seek(fd, phdr.p_offset + i);
					readn(fd, (void *)TMPPAGE, phdr.p_filesz - i);
					syscall_mem_map(0, TMPPAGE, child_envid,
									phdr.p_vaddr + i, PTE_V | PTE_R);
					syscall_mem_unmap(0, TMPPAGE);
				}

				//writef("DEBUG: i=%x, load at %X\n",i,phdr.p_vaddr + i);
			}

			while (i < phdr.p_memsz) {
				syscall_mem_alloc(child_envid, phdr.p_vaddr + i, PTE_V | PTE_R);
				//writef("DEBUG2:phdr.p_memsz=%x, i=%x, load at %X\n",phdr.p_memsz,i,phdr.p_vaddr+i);
				i += BY2PG;
			}
		}
	}

	return ehdr.e_entry;
}

int
init_stack(u_int child, char **argv, u_int *init_esp)
{
	int argc, i, r, tot;
	char *strings;
	u_int *args;

	// Count the number of arguments (argc)
	// and the total amount of space needed for strings (tot)
	tot = 0;

	for (argc = 0; argv[argc]; argc++) {
		tot += strlen(argv[argc]) + 1;
	}

	// Make sure everything will fit in the initial stack page
	if (ROUND(tot, 4) + 4 * (argc + 3) > BY2PG) {
		return -E_NO_MEM;
	}

	// Determine where to place the strings and the args array
	strings = (char *)TMPPAGETOP - tot;
	args = (u_int *)(TMPPAGETOP - ROUND(tot, 4) - 4 * (argc + 1));

	if ((r = syscall_mem_alloc(0, TMPPAGE, PTE_V | PTE_R)) < 0) {
		return r;
	}

	// Replace this with your code to:
	//
	//	- copy the argument strings into the stack page at 'strings'
	//printf("come 1\n");
	char *ctemp, *argv_temp;
	u_int j;
	ctemp = strings;

	//printf("tot=%d\n",tot);
	for (i = 0; i < argc; i++) {
		argv_temp = argv[i];

		for (j = 0; j < strlen(argv[i]); j++) {
			*ctemp = *argv_temp;
			ctemp++;
			argv_temp++;
		}

		*ctemp = 0;
		ctemp++;
	}

	//	- initialize args[0..argc-1] to be pointers to these strings
	//	  that will be valid addresses for the child environment
	//	  (for whom this page will be at USTACKTOP-BY2PG!).
	//printf("come 2\n");
	ctemp = (char *)(USTACKTOP - TMPPAGETOP + (u_int)strings);

	for (i = 0; i < argc; i++) {
		args[i] = (u_int)ctemp;
		//writef("args[i]=%x\n",args[i]);
		ctemp += strlen(argv[i]) + 1;
	}

	//	- set args[argc] to 0 to null-terminate the args array.
	//printf("come 3\n");
	ctemp--;
	args[argc] = (u_int)ctemp;
	//writef("args[argc]=%x\n",args[argc]);
	//	- push two more words onto the child's stack below 'args',
	//	  containing the argc and argv parameters to be passed
	//	  to the child's umain() function.
	u_int *pargv_ptr;
	//printf("come 4	args=%x\n",(u_int)args);
	pargv_ptr = args - 1;
	*pargv_ptr = USTACKTOP - TMPPAGETOP + (u_int)args;
	//writef("*pargv_ptr=%x\n",*pargv_ptr);
	pargv_ptr--;
	*pargv_ptr = argc;
	//
	//	- set *init_esp to the initial stack pointer for the child
	//
	//printf("come 5\n");
	//writef("TMPPAGETOP - pargv_ptr =%x,	pargv_ptr=%x\n",TMPPAGETOP - (u_int)pargv_ptr,pargv_ptr);
	*init_esp = USTACKTOP - TMPPAGETOP + (u_int)pargv_ptr;
	//	*init_esp = USTACKTOP;	// Change this!

	if ((r = syscall_mem_map(0, TMPPAGE, child, USTACKTOP - BY2PG,
							 PTE_V | PTE_R)) < 0) {
		goto error;
	}

	if ((r = syscall_mem_unmap(0, TMPPAGE)) < 0) {
		goto error;
	}

	return 0;

error:
	syscall_mem_unmap(0, TMPPAGE);
	return r;
}


int spawn(char *prog, char **argv)
{
	int fd, r;
	u_int32_t esp, entry_point;

	if ((fd = open(prog, O_RDWR/*O_ACCMODE*/)) < 0) {
		user_panic("spawn:open %s:%e", prog, fd);
	}

	u_int child_envid;
	child_envid = syscall_env_alloc();

	if (child_envid < 0) {
		writef("spawn:alloc the new env is wrong\n");
		return child_envid;
	}

	entry_point = load_elf_from_disk(fd, child_envid);

	init_stack(child_envid, argv, &esp);

	if (entry_point == 0) {
		return -1;
	}

	struct Trapframe *tf;

	writef("\n::::::::::spawn size : %x  sp : %x::::::::\n",
		   ((struct Filefd *)num2fd(fd))->f_file.f_size, esp);

	tf = &(envs[ENVX(child_envid)].env_tf);

	tf->pc = entry_point;

	tf->regs[29] = esp;


	u_int pdeno = 0;

	u_int pteno = 0;

	u_int pn = 0;

	u_int va = 0;

	//writef("spawn begin to share \n");
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {
		if (!((* vpd)[pdeno]&PTE_V)) {
			continue;
		}

		for (pteno = 0; pteno <= PTX(~0); pteno++) {
			pn = (pdeno << 10) + pteno;

			if (((* vpt)[pn]&PTE_V) && ((* vpt)[pn]&PTE_LIBRARY)) {
				va = pn * BY2PG;

				if ((r = syscall_mem_map(0, va, child_envid, va,
										 (PTE_V | PTE_R | PTE_LIBRARY))) < 0) {

					writef("va: %x   child_envid: %x   \n", va, child_envid);
					user_panic("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
					return r;
				}
			}
		}
	}


	if ((r = syscall_set_env_status(child_envid, ENV_RUNNABLE)) < 0) {
		writef("set child runnable is wrong\n");
		return r;
	}

	//writef("spawn:end of spawn\n");
	return child_envid;

}

int
spawnl(char *prog, char *args, ...)
{
	return spawn(prog, &args);
}
