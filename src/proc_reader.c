/**
 * proc_reader.c — /proc filesystem scanner and parser.
 *
 * Reads process information directly from the Linux /proc pseudo-filesystem.
 * Does NOT call ps, pstree, or any external tools.
 *
 * Key files used:
 *   /proc/<PID>/stat    — PID, comm, state, PPID, utime, stime, starttime
 *   /proc/<PID>/status  — VmRSS, Uid, Threads
 *   /proc/<PID>/cmdline — full command line
 *   /proc/stat          — system-wide CPU counters
 *   /proc/meminfo       — system memory totals
 */
#define _DEFAULT_SOURCE
#include "proc_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>

/* ------------------------------------------------------------------ */
/* Internal: parse /proc/<pid>/stat                                    */
/* ------------------------------------------------------------------ */

/**
 * Parse the relevant fields from /proc/<pid>/stat.
 *
 * The format is:
 *   pid (comm) state ppid pgroup session tty_nr ...
 *
 * We handle the tricky case where comm may contain spaces and parentheses
 * by finding the LAST ')' in the line.
 *
 * Returns 0 on success, -1 on failure.
 */
static int parse_stat(int pid, Process *p)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buf[4096];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Find the last ')' — end of comm field */
    char *comm_end = strrchr(buf, ')');
    if (!comm_end) return -1;

    /* Extract comm: between first '(' and last ')' */
    char *comm_start = strchr(buf, '(');
    if (!comm_start || comm_start >= comm_end) return -1;
    size_t comm_len = (size_t)(comm_end - comm_start - 1);
    if (comm_len >= sizeof(p->name)) comm_len = sizeof(p->name) - 1;
    strncpy(p->name, comm_start + 1, comm_len);
    p->name[comm_len] = '\0';

    /* Parse the rest of the fields after ')' */
    char state;
    int  ppid;
    unsigned long utime, stime;
    long start_time;

    /* Fields after last ')': state ppid pgroup session tty_nr tty_pgrp
       flags minflt cminflt majflt cmajflt utime stime cutime cstime
       priority nice num_threads itrealvalue starttime ... */
    int ret = sscanf(comm_end + 2,
                     "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u "
                     "%lu %lu %*d %*d %*d %*d %*d %*d %lu",
                     &state, &ppid,
                     &utime, &stime,
                     (unsigned long *)&start_time);
    if (ret < 5) return -1;

    p->pid        = pid;
    p->ppid       = ppid;
    p->state      = state;
    p->utime      = utime;
    p->stime      = stime;
    p->start_time = start_time;
    p->is_zombie  = (state == STATE_ZOMBIE);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Internal: parse /proc/<pid>/status                                  */
/* ------------------------------------------------------------------ */

static int parse_status(int pid, Process *p)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &p->vm_rss_kb);
        } else if (strncmp(line, "Uid:", 4) == 0) {
            /* Uid: real effective saved fs */
            int ruid = 0;
            sscanf(line + 4, "%d", &ruid);
            p->uid = ruid;
            /* Resolve username */
            struct passwd *pw = getpwuid((uid_t)ruid);
            if (pw) {
                strncpy(p->username, pw->pw_name, sizeof(p->username) - 1);
            } else {
                snprintf(p->username, sizeof(p->username), "%d", ruid);
            }
        } else if (strncmp(line, "Threads:", 8) == 0) {
            sscanf(line + 8, "%d", &p->threads);
        }
    }

    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Internal: parse /proc/<pid>/cmdline                                 */
/* ------------------------------------------------------------------ */

static void parse_cmdline(int pid, Process *p)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

    FILE *f = fopen(path, "r");
    if (!f) return;

    size_t n = fread(p->cmdline, 1, sizeof(p->cmdline) - 1, f);
    fclose(f);

    /* cmdline is NUL-separated; replace NULs with spaces */
    for (size_t i = 0; i < n; i++) {
        if (p->cmdline[i] == '\0') p->cmdline[i] = ' ';
    }
    p->cmdline[n] = '\0';

    /* Trim trailing spaces */
    while (n > 0 && p->cmdline[n - 1] == ' ') {
        p->cmdline[--n] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Public: read one process                                            */
/* ------------------------------------------------------------------ */

Process *proc_read_one(int pid)
{
    Process *p = process_create();
    if (!p) return NULL;

    if (parse_stat(pid, p) < 0) {
        /* Process vanished between directory scan and stat read — OK */
        process_free(p);
        return NULL;
    }

    parse_status(pid, p);   /* best-effort */
    parse_cmdline(pid, p);  /* best-effort */

    /* If cmdline is empty, use name */
    if (p->cmdline[0] == '\0') {
        snprintf(p->cmdline, sizeof(p->cmdline), "[%s]", p->name);
    }

    return p;
}

/* ------------------------------------------------------------------ */
/* Public: scan all processes                                          */
/* ------------------------------------------------------------------ */

Process **proc_read_all(int *out_count)
{
    if (!out_count) return NULL;
    *out_count = 0;

    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return NULL;

    /* Initial capacity */
    int cap = 512;
    Process **arr = malloc((size_t)cap * sizeof(Process *));
    if (!arr) {
        closedir(proc_dir);
        return NULL;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(proc_dir)) != NULL) {
        /* We only care about numeric directories */
        const char *name = entry->d_name;
        if (!isdigit((unsigned char)name[0])) continue;

        /* Verify it's all digits */
        int all_digits = 1;
        for (int i = 1; name[i]; i++) {
            if (!isdigit((unsigned char)name[i])) { all_digits = 0; break; }
        }
        if (!all_digits) continue;

        int pid = atoi(name);
        if (pid <= 0) continue;

        Process *p = proc_read_one(pid);
        if (!p) continue;   /* vanished — skip */

        /* Grow array if needed */
        if (count >= cap) {
            cap *= 2;
            Process **tmp = realloc(arr, (size_t)cap * sizeof(Process *));
            if (!tmp) {
                process_free(p);
                break;
            }
            arr = tmp;
        }

        arr[count++] = p;
    }

    closedir(proc_dir);
    *out_count = count;
    return arr;
}

/* ------------------------------------------------------------------ */
/* System-wide CPU info                                                */
/* ------------------------------------------------------------------ */

int proc_read_system_cpu(unsigned long *total_time, unsigned long *idle_time)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    unsigned long user, nice, system, idle, iowait, irq, softirq;
    int ret = fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu",
                     &user, &nice, &system, &idle,
                     &iowait, &irq, &softirq);
    fclose(f);

    if (ret < 7) return -1;

    *idle_time  = idle + iowait;
    *total_time = user + nice + system + idle + iowait + irq + softirq;
    return 0;
}

/* ------------------------------------------------------------------ */
/* System-wide memory info                                             */
/* ------------------------------------------------------------------ */

int proc_read_system_mem(long *total_kb, long *available_kb)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    *total_kb     = 0;
    *available_kb = 0;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%ld", total_kb);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%ld", available_kb);
        }
        if (*total_kb && *available_kb) break;
    }

    fclose(f);
    return (*total_kb > 0) ? 0 : -1;
}
