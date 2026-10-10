/**
 * analyzer.c — Process state and resource analysis implementation.
 */
#define _DEFAULT_SOURCE
#include "analyzer.h"
#include "proc_reader.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* CpuState lifecycle                                                  */
/* ------------------------------------------------------------------ */

CpuState *cpu_state_create(void)
{
    return calloc(1, sizeof(CpuState));
}

void cpu_state_free(CpuState *cs)
{
    free(cs);
}

/* ------------------------------------------------------------------ */
/* CPU usage computation                                               */
/* ------------------------------------------------------------------ */

void analyzer_compute_cpu(ProcessTree *t, CpuState *cs)
{
    if (!t || !cs) return;

    unsigned long total_now = 0, idle_now = 0;
    if (proc_read_system_cpu(&total_now, &idle_now) < 0) return;

    unsigned long delta_total = total_now - cs->prev_total_cpu;
    if (delta_total == 0) {
        cs->prev_total_cpu = total_now;
        return;
    }

    for (int i = 0; i < t->count; i++) {
        Process *p = t->all[i];
        if (p->pid <= 0 || p->pid >= MAX_PROCESSES) {
            p->cpu_usage = 0.0f;
            continue;
        }

        unsigned long prev_u = cs->prev_utime[p->pid];
        unsigned long prev_s = cs->prev_stime[p->pid];
        unsigned long delta_proc = (p->utime + p->stime) - (prev_u + prev_s);

        p->cpu_usage = (float)delta_proc / (float)delta_total * 100.0f;
        if (p->cpu_usage < 0.0f) p->cpu_usage = 0.0f;
        if (p->cpu_usage > 100.0f) p->cpu_usage = 100.0f;

        /* Save for next cycle */
        cs->prev_utime[p->pid] = p->utime;
        cs->prev_stime[p->pid] = p->stime;
    }

    cs->prev_total_cpu = total_now;
}

/* ------------------------------------------------------------------ */
/* Re-parenting detection                                              */
/* ------------------------------------------------------------------ */

void analyzer_detect_reparenting(ProcessTree *prev_tree,
                                 ProcessTree *curr_tree)
{
    if (!curr_tree) return;

    for (int i = 0; i < curr_tree->count; i++) {
        Process *cur = curr_tree->all[i];
        cur->is_reparented = 0;
        cur->prev_ppid     = cur->ppid;  /* default: same */

        if (!prev_tree) continue;

        /* Look up this PID in the previous tree */
        Process *prev = tree_find_pid(prev_tree, cur->pid);
        if (!prev) continue;  /* new process — not a re-parenting event */

        cur->prev_ppid = prev->ppid;
        if (cur->ppid != prev->ppid) {
            cur->is_reparented = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Top-N helpers (simple selection sort for small N)                  */
/* ------------------------------------------------------------------ */

static int cmp_cpu_desc(const void *a, const void *b)
{
    const Process *pa = *(const Process **)a;
    const Process *pb = *(const Process **)b;
    if (pb->cpu_usage > pa->cpu_usage) return  1;
    if (pb->cpu_usage < pa->cpu_usage) return -1;
    return 0;
}

static int cmp_mem_desc(const void *a, const void *b)
{
    const Process *pa = *(const Process **)a;
    const Process *pb = *(const Process **)b;
    if (pb->vm_rss_kb > pa->vm_rss_kb) return  1;
    if (pb->vm_rss_kb < pa->vm_rss_kb) return -1;
    return 0;
}

int analyzer_top_cpu(ProcessTree *t, Process **out_list, int n)
{
    if (!t || !out_list || n <= 0) return 0;
    /* Copy pointers, sort, take first n */
    Process **tmp = malloc((size_t)t->count * sizeof(Process *));
    if (!tmp) return 0;
    memcpy(tmp, t->all, (size_t)t->count * sizeof(Process *));
    qsort(tmp, (size_t)t->count, sizeof(Process *), cmp_cpu_desc);
    int take = (t->count < n) ? t->count : n;
    for (int i = 0; i < take; i++) out_list[i] = tmp[i];
    free(tmp);
    return take;
}

int analyzer_top_mem(ProcessTree *t, Process **out_list, int n)
{
    if (!t || !out_list || n <= 0) return 0;
    Process **tmp = malloc((size_t)t->count * sizeof(Process *));
    if (!tmp) return 0;
    memcpy(tmp, t->all, (size_t)t->count * sizeof(Process *));
    qsort(tmp, (size_t)t->count, sizeof(Process *), cmp_mem_desc);
    int take = (t->count < n) ? t->count : n;
    for (int i = 0; i < take; i++) out_list[i] = tmp[i];
    free(tmp);
    return take;
}
