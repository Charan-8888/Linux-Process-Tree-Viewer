/**
 * analyzer.h — Process state and resource analysis.
 *
 * Detects zombies, re-parenting, computes per-process CPU usage, and
 * identifies top resource consumers.
 */
#ifndef ANALYZER_H
#define ANALYZER_H

#include "process.h"
#include "tree.h"

/* ------------------------------------------------------------------ */
/* CPU usage computation                                               */
/* ------------------------------------------------------------------ */

/**
 * State captured between two snapshots for CPU % calculation.
 * Stores the previous per-process CPU counters and system-wide totals.
 */
typedef struct {
    unsigned long prev_utime[MAX_PROCESSES];
    unsigned long prev_stime[MAX_PROCESSES];
    unsigned long prev_total_cpu;   /* /proc/stat total jiffies */
    unsigned long prev_idle_cpu;    /* /proc/stat idle jiffies  */
} CpuState;

/**
 * Allocate a zeroed CpuState.
 */
CpuState *cpu_state_create(void);
void      cpu_state_free(CpuState *cs);

/**
 * Compute per-process CPU% using:
 *   Δ(utime + stime) / Δ(system total) × 100
 *
 * Should be called once per refresh cycle BEFORE updating prev counters.
 */
void analyzer_compute_cpu(ProcessTree *t, CpuState *cs);

/* ------------------------------------------------------------------ */
/* Re-parenting detection                                              */
/* ------------------------------------------------------------------ */

/**
 * Compare current PPIDs against the previous-snapshot PPIDs stored in
 * prev_ppid.  Sets p->is_reparented = 1 if the PPID changed.
 *
 * @param prev_tree  Snapshot from the previous cycle (may be NULL on
 *                   the first run — in that case nothing is marked).
 * @param curr_tree  Current snapshot.
 */
void analyzer_detect_reparenting(ProcessTree *prev_tree,
                                 ProcessTree *curr_tree);

/* ------------------------------------------------------------------ */
/* Resource alerts                                                     */
/* ------------------------------------------------------------------ */

#define CPU_ALERT_THRESHOLD   80.0f   /* % CPU above which we warn   */
#define MEM_ALERT_THRESHOLD_KB (512 * 1024)  /* 512 MB in KB          */

/**
 * Populate out_list with pointers to the top-N CPU consumers.
 * out_list must have room for n entries.
 * Returns the number of entries actually written.
 */
int analyzer_top_cpu(ProcessTree *t, Process **out_list, int n);

/**
 * Populate out_list with pointers to the top-N memory consumers.
 */
int analyzer_top_mem(ProcessTree *t, Process **out_list, int n);

#endif /* ANALYZER_H */
