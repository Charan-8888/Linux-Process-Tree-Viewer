/**
 * monitor.h — Refresh loop, snapshot comparison, and event log.
 *
 * Runs a background pthread that wakes up every REFRESH_INTERVAL_MS
 * milliseconds, calls proc_read_all(), builds a new tree, runs analysis,
 * and swaps the current snapshot.  Events (process created / terminated)
 * are pushed to a ring buffer that the UI reads.
 */
#ifndef MONITOR_H
#define MONITOR_H

#include "process.h"
#include "tree.h"
#include "analyzer.h"

/* Refresh interval default (milliseconds) */
#define REFRESH_INTERVAL_MS 1000

/* Maximum number of events kept in the ring buffer */
#define EVENT_RING_SIZE 256

/* ------------------------------------------------------------------ */
/* Event log                                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    EVENT_CREATED,
    EVENT_TERMINATED,
    EVENT_ZOMBIE,
    EVENT_REPARENTED,
} EventType;

typedef struct {
    EventType   type;
    int         pid;
    char        name[256];
    int         prev_ppid;   /* for REPARENTED events */
    int         new_ppid;    /* for REPARENTED events */
    /* Wall-clock time of the event */
    int         hour, min, sec;
} ProcessEvent;

/* ------------------------------------------------------------------ */
/* Monitor state                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Current live tree (protected by mutex) */
    ProcessTree *tree;

    /* Event ring buffer */
    ProcessEvent events[EVENT_RING_SIZE];
    int          event_head;   /* next write position  */
    int          event_count;  /* number of valid entries */

    /* Named snapshots (simple array of trees, at most 8) */
    ProcessTree *snapshots[8];
    char         snapshot_names[8][64];
    int          snapshot_count;

    /* CPU state for delta calculations */
    CpuState    *cpu_state;

    /* Refresh interval */
    int          interval_ms;

    /* Internal pthread and mutex (opaque to UI) */
    void        *_thread;   /* pthread_t stored as void* to avoid exposing pthread.h */
    void        *_mutex;    /* pthread_mutex_t* */
    int          _running;

    /* System-wide stats (updated every cycle) */
    float        sys_cpu_percent;
    float        sys_mem_percent;
    long         sys_mem_total_kb;
    long         sys_mem_avail_kb;
} Monitor;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/** Allocate and start the monitor (spawns refresh thread). */
Monitor *monitor_start(int interval_ms);

/** Stop the refresh thread and free all resources. */
void monitor_stop(Monitor *m);

/* ------------------------------------------------------------------ */
/* Tree access (thread-safe)                                           */
/* ------------------------------------------------------------------ */

/**
 * Lock the monitor mutex and return the current tree.
 * The caller MUST call monitor_unlock() when done.
 */
ProcessTree *monitor_lock_tree(Monitor *m);
void         monitor_unlock(Monitor *m);

/* ------------------------------------------------------------------ */
/* Event access                                                        */
/* ------------------------------------------------------------------ */

/**
 * Copy up to 'max' events from the ring buffer into 'out'.
 * Returns the number of events copied (most recent first).
 * Thread-safe.
 */
int monitor_get_events(Monitor *m, ProcessEvent *out, int max);

/* ------------------------------------------------------------------ */
/* Snapshots                                                           */
/* ------------------------------------------------------------------ */

/**
 * Save the current tree as a named snapshot.
 * Returns 0 on success, -1 if the snapshot table is full.
 */
int monitor_save_snapshot(Monitor *m, const char *name);

/**
 * Compare snapshot[a_idx] with snapshot[b_idx] and push diffs to
 * the event log.
 */
void monitor_compare_snapshots(Monitor *m, int a_idx, int b_idx);

/* ------------------------------------------------------------------ */
/* Search (delegates to tree_search on the live tree)                 */
/* ------------------------------------------------------------------ */

/**
 * Run a search query on the live tree.
 * Returns number of matches.
 */
int monitor_search(Monitor *m, const char *query);

#endif /* MONITOR_H */
