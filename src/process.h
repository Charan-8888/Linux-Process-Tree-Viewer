/**
 * process.h — Process data model for Process Tree Viewer & Analyzer
 *
 * Defines the Process struct and helper functions for managing process
 * records read from /proc.
 */
#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>

/* Maximum number of children we pre-allocate per node */
#define INIT_CHILD_CAP 8

/* Process states as reported by /proc/<PID>/stat */
#define STATE_RUNNING       'R'
#define STATE_SLEEPING      'S'
#define STATE_DISK_SLEEP    'D'
#define STATE_STOPPED       'T'
#define STATE_ZOMBIE        'Z'
#define STATE_IDLE          'I'
#define STATE_DEAD          'X'

/**
 * Core process record.
 * Populated by proc_reader; linked by tree builder.
 */
typedef struct Process {
    /* Identification */
    int   pid;
    int   ppid;
    char  name[256];        /* comm field from /proc/<PID>/stat      */
    char  cmdline[1024];    /* full command from /proc/<PID>/cmdline */

    /* State */
    char  state;            /* single char: R S D T Z I X            */
    int   uid;              /* real UID from /proc/<PID>/status      */
    char  username[64];     /* resolved username (best-effort)       */

    /* Resources */
    long  vm_rss_kb;        /* resident set size in KB               */
    int   threads;          /* thread count                          */

    /* CPU accounting (raw jiffies from /proc/<PID>/stat) */
    unsigned long utime;    /* user-mode time                        */
    unsigned long stime;    /* kernel-mode time                      */
    float cpu_usage;        /* computed % between two snapshots      */

    /* Timing */
    long  start_time;       /* process start time (clock ticks)     */

    /* Tree links — filled by tree builder */
    struct Process *parent;
    struct Process **children;
    int   child_count;
    int   child_cap;

    /* Analysis flags */
    int   is_zombie;        /* state == 'Z'                          */
    int   is_reparented;    /* ppid changed since last snapshot      */
    int   prev_ppid;        /* ppid in previous snapshot             */
    int   is_new;           /* appeared in this snapshot             */

    /* UI state */
    int   collapsed;        /* subtree is collapsed in TUI           */
    int   highlighted;      /* matched a search query                */
} Process;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/** Allocate and zero-initialise a Process. Returns NULL on OOM. */
Process *process_create(void);

/** Free a Process node (does NOT free children). */
void process_free(Process *p);

/** Add child c to parent p, growing the children array as needed. */
int process_add_child(Process *parent, Process *child);

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Return a human-readable state string. */
const char *process_state_str(char state);

/** Return true if this is a zombie process. */
int process_is_zombie(const Process *p);

/** Copy fields from src into dst (tree links are NOT copied). */
void process_copy_fields(Process *dst, const Process *src);

#endif /* PROCESS_H */
