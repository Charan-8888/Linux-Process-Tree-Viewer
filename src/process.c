/**
 * process.c — Implementation of Process data model helpers.
 */
#include "process.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pwd.h>

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

Process *process_create(void)
{
    Process *p = calloc(1, sizeof(Process));
    if (!p) return NULL;

    p->child_cap = INIT_CHILD_CAP;
    p->children  = calloc((size_t)p->child_cap, sizeof(Process *));
    if (!p->children) {
        free(p);
        return NULL;
    }
    return p;
}

void process_free(Process *p)
{
    if (!p) return;
    free(p->children);
    free(p);
}

int process_add_child(Process *parent, Process *child)
{
    if (!parent || !child) return -1;

    /* Grow children array if needed */
    if (parent->child_count >= parent->child_cap) {
        int new_cap = parent->child_cap * 2;
        Process **tmp = realloc(parent->children,
                                (size_t)new_cap * sizeof(Process *));
        if (!tmp) return -1;
        parent->children = tmp;
        parent->child_cap = new_cap;
    }

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

const char *process_state_str(char state)
{
    switch (state) {
        case STATE_RUNNING:    return "Running";
        case STATE_SLEEPING:   return "Sleeping";
        case STATE_DISK_SLEEP: return "Disk Sleep";
        case STATE_STOPPED:    return "Stopped";
        case STATE_ZOMBIE:     return "Zombie";
        case STATE_IDLE:       return "Idle";
        case STATE_DEAD:       return "Dead";
        default:               return "Unknown";
    }
}

int process_is_zombie(const Process *p)
{
    return p && (p->state == STATE_ZOMBIE);
}

void process_copy_fields(Process *dst, const Process *src)
{
    if (!dst || !src) return;
    int   save_cc   = dst->child_count;
    int   save_cap  = dst->child_cap;
    Process **save_ch = dst->children;
    Process  *save_par = dst->parent;
    int   save_coll = dst->collapsed;

    *dst = *src;

    dst->child_count = save_cc;
    dst->child_cap   = save_cap;
    dst->children    = save_ch;
    dst->parent      = save_par;
    dst->collapsed   = save_coll;
}
