#include <env.h>
#include <pmap.h>
#include <printf.h>

// Trivial temporary clock interrupt handler,
// called from clock_interrupt in locore.S
/*void
clock(void)
{
	printf("*");
}*/


// The real clock interrupt handler,
// implementing round-robin scheduling
void
sched_yield(void)
{
	static int Next_EnvNum = 0;
	int iTemp, i;

	for (i = 0; i < NENV; i++) {	//envs[0] must be runnable
		if (envs[Next_EnvNum].env_status == ENV_RUNNABLE) {
			iTemp = Next_EnvNum;
			Next_EnvNum++;
			Next_EnvNum = Next_EnvNum % NENV;

			if (iTemp != 0) {
				//panic("BBBBBBBBBBBBBBBBBBBB id:%x",iTemp);
				env_run(&envs[iTemp]);
			}
		} else {
			Next_EnvNum++;
			Next_EnvNum = Next_EnvNum % NENV;
		}
	}

	//env_run(&envs[0]);
	panic("There is no process to run ! haha !\n");
}

