/**
 * test_reparent.c — Controlled process re-parenting demonstration.
 *
 * OS Concept: If a parent process exits while its child is still running,
 * the child is "orphaned" and re-parented.  On Linux the new parent is
 * typically PID 1 (init/systemd) or a configured subreaper.
 *
 * Hierarchy:
 *   grandparent (this process)
 *     └── parent
 *           └── grandchild (long-running)
 *
 * Sequence:
 *   1. grandparent forks parent.
 *   2. parent forks grandchild (which runs for 60 s).
 *   3. parent exits immediately (without wait()).
 *   4. grandchild's PPID changes — ptva detects and highlights this.
 *
 * Build:
 *   gcc -Wall -std=c11 test_reparent.c -o test_reparent
 *
 * Run:
 *   ./test_reparent
 *   # Open ptva and watch the grandchild's PPID change to 1
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define GRANDCHILD_LIFE_SEC 60   /* grandchild stays alive this long */
#define PARENT_DELAY_SEC    3    /* parent lives a little before dying */

int main(void)
{
    printf("[reparent demo] Grandparent PID: %d\n", getpid());
    fflush(stdout);

    pid_t parent_pid = fork();

    if (parent_pid < 0) { perror("fork (parent)"); return 1; }

    if (parent_pid == 0) {
        /* ==== PARENT PROCESS ==== */
        printf("[reparent demo] Parent PID: %d  (PPID: %d)\n",
               getpid(), getppid());
        fflush(stdout);

        pid_t grandchild = fork();
        if (grandchild < 0) { perror("fork (grandchild)"); exit(1); }

        if (grandchild == 0) {
            /* ==== GRANDCHILD PROCESS ==== */
            printf("[reparent demo] Grandchild PID: %d  Initial PPID: %d\n",
                   getpid(), getppid());
            printf("[reparent demo] Grandchild will run for %d s — "
                   "watch its PPID change in ptva!\n", GRANDCHILD_LIFE_SEC);
            fflush(stdout);

            for (int i = 0; i < GRANDCHILD_LIFE_SEC; i++) {
                sleep(1);
                if (i % 5 == 0) {
                    printf("[reparent demo] Grandchild PID %d  current PPID: %d  "
                           "(%d/%d s)\n",
                           getpid(), getppid(), i, GRANDCHILD_LIFE_SEC);
                    fflush(stdout);
                }
            }
            printf("[reparent demo] Grandchild exiting.\n");
            exit(0);
        }

        /* ==== BACK IN PARENT ==== */
        printf("[reparent demo] Parent spawned grandchild PID %d. "
               "Parent sleeping %d s then exiting...\n",
               grandchild, PARENT_DELAY_SEC);
        fflush(stdout);
        sleep(PARENT_DELAY_SEC);

        /* Parent exits WITHOUT waiting — grandchild becomes orphan */
        printf("[reparent demo] Parent exiting (grandchild %d will be re-parented)\n",
               grandchild);
        fflush(stdout);
        exit(0);
    }

    /* ==== GRANDPARENT ==== */
    printf("[reparent demo] Grandparent waiting for parent (PID %d) to finish...\n",
           parent_pid);
    fflush(stdout);

    int status;
    waitpid(parent_pid, &status, 0);

    printf("[reparent demo] Parent collected. "
           "Grandchild should now be re-parented.\n");
    printf("[reparent demo] Grandparent sleeping while grandchild runs...\n");
    fflush(stdout);

    sleep(GRANDCHILD_LIFE_SEC + 5);

    printf("[reparent demo] Done.\n");
    return 0;
}
