/**
 * monitor.c — Refresh loop, snapshot comparison, and event log.
 *
 * Spawns a background pthread that periodically:
 *  1. Calls proc_read_all() to get a fresh flat process list.
 *  2. Builds a new ProcessTree.
 *  3. Runs CPU and re-parenting analysis.
 *  4. Diffs old vs. new to generate events.
 *  5. Swaps the current tree under a mutex.
 */
#define _DEFAULT_SOURCE
#include "monitor.h"
#include "proc_reader.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void push_event(Monitor *m, EventType type, Process *p,
                        int prev_ppid, int new_ppid)
{
    if (!m) return;
    ProcessEvent *ev = &m->events[m->event_head % EVENT_RING_SIZE];

    ev->type     = type;
    ev->pid      = p->pid;
    ev->prev_ppid = prev_ppid;
    ev->new_ppid  = new_ppid;
    strncpy(ev->name, p->name, sizeof(ev->name) - 1);
    ev->name[sizeof(ev->name) - 1] = '\0';

    /* Timestamp */
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    ev->hour = tm_info->tm_hour;
    ev->min  = tm_info->tm_min;
    ev->sec  = tm_info->tm_sec;

    m->event_head = (m->event_head + 1) % EVENT_RING_SIZE;
    if (m->event_count < EVENT_RING_SIZE) m->event_count++;
}

/* Diff old tree vs. new tree, push events */
static void diff_trees(Monitor *m, ProcessTree *old_tree,
                        ProcessTree *new_tree)
{
    if (!old_tree || !new_tree) return;

    /* Detect terminated: PID in old but not in new */
    for (int i = 0; i < old_tree->count; i++) {
        Process *op = old_tree->all[i];
        if (!tree_find_pid(new_tree, op->pid)) {
            push_event(m, EVENT_TERMINATED, op, 0, 0);
        }
    }

    /* Detect created: PID in new but not in old */
    for (int i = 0; i < new_tree->count; i++) {
        Process *np = new_tree->all[i];
        if (!tree_find_pid(old_tree, np->pid)) {
            np->is_new = 1;
            push_event(m, EVENT_CREATED, np, 0, 0);
        }
    }

    /* Detect zombies and re-parentings */
    for (int i = 0; i < new_tree->count; i++) {
        Process *np = new_tree->all[i];
        if (np->is_zombie) {
            push_event(m, EVENT_ZOMBIE, np, 0, 0);
        }
        if (np->is_reparented) {
            push_event(m, EVENT_REPARENTED, np, np->prev_ppid, np->ppid);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Background refresh thread                                           */
/* ------------------------------------------------------------------ */

static void *refresh_thread(void *arg)
{
    Monitor *m = (Monitor *)arg;
    pthread_mutex_t *mutex = (pthread_mutex_t *)m->_mutex;

    while (m->_running) {
        /* Sleep for interval */
        struct timespec ts;
        ts.tv_sec  = m->interval_ms / 1000;
        ts.tv_nsec = (long)(m->interval_ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);

        if (!m->_running) break;

        /* Build new tree */
        int count = 0;
        Process **procs = proc_read_all(&count);
        if (!procs) continue;

        ProcessTree *new_tree = tree_build(procs, count);
        if (!new_tree) {
            /* clean up */
            for (int i = 0; i < count; i++) process_free(procs[i]);
            free(procs);
            continue;
        }

        /* Run analysis before swapping */
        analyzer_compute_cpu(new_tree, m->cpu_state);

        pthread_mutex_lock(mutex);

        ProcessTree *old_tree = m->tree;
        analyzer_detect_reparenting(old_tree, new_tree);
        diff_trees(m, old_tree, new_tree);

        /* Update system-wide stats */
        long total_kb = 0, avail_kb = 0;
        if (proc_read_system_mem(&total_kb, &avail_kb) == 0 && total_kb > 0) {
            m->sys_mem_total_kb = total_kb;
            m->sys_mem_avail_kb = avail_kb;
            m->sys_mem_percent  = (float)(total_kb - avail_kb) / (float)total_kb * 100.0f;
        }

        unsigned long cpu_total = 0, cpu_idle = 0;
        if (proc_read_system_cpu(&cpu_total, &cpu_idle) == 0) {
            unsigned long prev_total = m->cpu_state->prev_total_cpu;
            unsigned long prev_idle  = m->cpu_state->prev_idle_cpu;
            unsigned long dt = (cpu_total > prev_total) ? cpu_total - prev_total : 1;
            unsigned long di = (cpu_idle  > prev_idle)  ? cpu_idle  - prev_idle  : 0;
            m->sys_cpu_percent = 100.0f - ((float)di / (float)dt * 100.0f);
            if (m->sys_cpu_percent < 0.0f)   m->sys_cpu_percent = 0.0f;
            if (m->sys_cpu_percent > 100.0f) m->sys_cpu_percent = 100.0f;
            m->cpu_state->prev_idle_cpu = cpu_idle;
        }

        m->tree = new_tree;
        pthread_mutex_unlock(mutex);

        /* Free old tree */
        if (old_tree) tree_free(old_tree);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

Monitor *monitor_start(int interval_ms)
{
    Monitor *m = calloc(1, sizeof(Monitor));
    if (!m) return NULL;

    m->interval_ms = (interval_ms > 0) ? interval_ms : REFRESH_INTERVAL_MS;
    m->cpu_state   = cpu_state_create();
    if (!m->cpu_state) { free(m); return NULL; }

    /* Allocate mutex */
    pthread_mutex_t *mutex = malloc(sizeof(pthread_mutex_t));
    if (!mutex) { cpu_state_free(m->cpu_state); free(m); return NULL; }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    m->_mutex = mutex;

    /* Do an initial synchronous scan so the UI has data immediately */
    int count = 0;
    Process **procs = proc_read_all(&count);
    if (procs) {
        m->tree = tree_build(procs, count);
        analyzer_compute_cpu(m->tree, m->cpu_state);
    }

    /* Start refresh thread */
    m->_running = 1;
    pthread_t *tid = malloc(sizeof(pthread_t));
    if (!tid) {
        /* non-fatal — will run without background refresh */
        m->_thread = NULL;
    } else {
        if (pthread_create(tid, NULL, refresh_thread, m) != 0) {
            free(tid);
            m->_thread = NULL;
        } else {
            m->_thread = tid;
        }
    }

    return m;
}

void monitor_stop(Monitor *m)
{
    if (!m) return;

    m->_running = 0;

    if (m->_thread) {
        pthread_join(*(pthread_t *)m->_thread, NULL);
        free(m->_thread);
    }

    if (m->_mutex) {
        pthread_mutex_destroy((pthread_mutex_t *)m->_mutex);
        free(m->_mutex);
    }

    if (m->tree) tree_free(m->tree);

    for (int i = 0; i < m->snapshot_count; i++) {
        if (m->snapshots[i]) tree_free(m->snapshots[i]);
    }

    cpu_state_free(m->cpu_state);
    free(m);
}

/* ------------------------------------------------------------------ */
/* Thread-safe tree access                                             */
/* ------------------------------------------------------------------ */

ProcessTree *monitor_lock_tree(Monitor *m)
{
    if (!m) return NULL;
    pthread_mutex_lock((pthread_mutex_t *)m->_mutex);
    return m->tree;
}

void monitor_unlock(Monitor *m)
{
    if (!m) return;
    pthread_mutex_unlock((pthread_mutex_t *)m->_mutex);
}

/* ------------------------------------------------------------------ */
/* Event access                                                        */
/* ------------------------------------------------------------------ */

int monitor_get_events(Monitor *m, ProcessEvent *out, int max)
{
    if (!m || !out || max <= 0) return 0;
    pthread_mutex_lock((pthread_mutex_t *)m->_mutex);

    int count = (m->event_count < max) ? m->event_count : max;
    /* Copy most-recent events first */
    for (int i = 0; i < count; i++) {
        int idx = ((m->event_head - 1 - i) % EVENT_RING_SIZE + EVENT_RING_SIZE)
                  % EVENT_RING_SIZE;
        out[i] = m->events[idx];
    }

    pthread_mutex_unlock((pthread_mutex_t *)m->_mutex);
    return count;
}

/* ------------------------------------------------------------------ */
/* Snapshots                                                           */
/* ------------------------------------------------------------------ */

/* Deep-copy a single Process node (no tree links) */
static Process *clone_process(const Process *src)
{
    Process *p = process_create();
    if (!p) return NULL;
    /* Save the freshly-allocated children array before memcpy overwrites it */
    Process **fresh_children = p->children;
    /* Copy all scalar fields */
    memcpy(p, src, sizeof(Process));
    /* Restore / reset tree-link fields */
    p->child_count = 0;
    p->child_cap   = INIT_CHILD_CAP;
    p->children    = fresh_children;  /* reuse the one we already allocated */
    p->parent      = NULL;
    return p;
}

int monitor_save_snapshot(Monitor *m, const char *name)
{
    if (!m || m->snapshot_count >= 8) return -1;

    ProcessTree *live = monitor_lock_tree(m);
    if (!live) { monitor_unlock(m); return -1; }

    int snap_count = live->count;  /* capture before unlock */

    /* Clone all processes (while holding the lock) */
    Process **arr = malloc((size_t)snap_count * sizeof(Process *));
    if (!arr) { monitor_unlock(m); return -1; }

    for (int i = 0; i < snap_count; i++) {
        arr[i] = clone_process(live->all[i]);
        if (!arr[i]) {
            for (int j = 0; j < i; j++) process_free(arr[j]);
            free(arr);
            monitor_unlock(m);
            return -1;
        }
    }

    monitor_unlock(m);

    ProcessTree *snap = tree_build(arr, snap_count);
    if (!snap) return -1;

    int idx = m->snapshot_count++;
    m->snapshots[idx] = snap;
    if (name) {
        strncpy(m->snapshot_names[idx], name,
                sizeof(m->snapshot_names[idx]) - 1);
    } else {
        snprintf(m->snapshot_names[idx], sizeof(m->snapshot_names[idx]),
                 "snap%d", idx);
    }
    return idx;
}

void monitor_compare_snapshots(Monitor *m, int a_idx, int b_idx)
{
    if (!m) return;
    if (a_idx < 0 || a_idx >= m->snapshot_count) return;
    if (b_idx < 0 || b_idx >= m->snapshot_count) return;

    ProcessTree *a = m->snapshots[a_idx];
    ProcessTree *b = m->snapshots[b_idx];

    pthread_mutex_lock((pthread_mutex_t *)m->_mutex);
    diff_trees(m, a, b);
    pthread_mutex_unlock((pthread_mutex_t *)m->_mutex);
}

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

int monitor_search(Monitor *m, const char *query)
{
    if (!m) return 0;
    ProcessTree *t = monitor_lock_tree(m);
    int n = tree_search(t, query);
    monitor_unlock(m);
    return n;
}
