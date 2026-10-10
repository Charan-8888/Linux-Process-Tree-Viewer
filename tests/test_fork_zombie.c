/**
 * test_fork_zombie.c — Controlled zombie process demonstration.
 *
 * OS Concept: When a child exits, it becomes a zombie (state Z) until its
 * parent calls wait() / waitpid().  This program deliberately delays the
 * wait() call so that the zombie is visible to PTVA.
 *
 * Build:
 *   gcc -Wall -std=c11 test_fork_zombie.c -o test_fork_zombie
 *
 * Run:
 *   ./test_fork_zombie &
 *   # Open ptva in another terminal and search for "zombie_child"
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define ZOMBIE_WINDOW_SEC 15    /* how long the zombie is visible */
#define PARENT_SLEEP_SEC  30    /* total parent lifetime          */

int main(void)
{
    printf("[zombie demo] Parent PID: %d\n", getpid());
    fflush(stdout);

    pid_t child = fork();

    if (child < 0) {
        perror("fork");
        return 1;
    }

    if (child == 0) {
        /* ---- CHILD ---- */
        /* Rename for easy identification in ptva */
        printf("[zombie demo] Child PID %d exiting immediately "
               "(will become zombie for ~%d s)\n",
               getpid(), ZOMBIE_WINDOW_SEC);
        fflush(stdout);
        /* Child exits — it will appear as state Z until parent waits */
        exit(0);
    }

    /* ---- PARENT ---- */
    printf("[zombie demo] Child PID: %d\n", child);
    printf("[zombie demo] Sleeping %d s before wait() — "
           "child is a zombie during this time.\n", ZOMBIE_WINDOW_SEC);
    printf("[zombie demo] Run ptva and look for PID %d (state Z)\n", child);
    fflush(stdout);

    sleep(ZOMBIE_WINDOW_SEC);

    printf("[zombie demo] Calling wait() now...\n");
    fflush(stdout);

    int status;
    waitpid(child, &status, 0);

    printf("[zombie demo] Zombie collected. "
           "Child exited with status %d.\n",
           WEXITSTATUS(status));
    printf("[zombie demo] Parent sleeping %d more seconds...\n",
           PARENT_SLEEP_SEC - ZOMBIE_WINDOW_SEC);
    fflush(stdout);

    sleep(PARENT_SLEEP_SEC - ZOMBIE_WINDOW_SEC);

    printf("[zombie demo] Done.\n");
    return 0;
}
