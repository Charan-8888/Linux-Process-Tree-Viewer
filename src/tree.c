/**
 * tree.c — Process tree builder, traversal, and search.
 *
 * Algorithm:
 *  1. Insert all processes into a hash map (pid % MAX_PROCESSES with
 *     linear probing is too slow for collisions; we use direct indexing
 *     since PIDs are bounded by the kernel's PID_MAX, typically 32768 or
 *     4194304; we cap at MAX_PROCESSES for simplicity and skip processes
 *     with PID >= MAX_PROCESSES — extremely rare on normal systems).
 *  2. For each process, look up its PPID in the hash map.
 *     If found, add as a child.  If not found (parent died or is the
 *     kernel's virtual PID 0), attach it to our virtual root.
 *  3. The virtual root is PID 1 if present; otherwise a synthetic node.
 */
#define _DEFAULT_SOURCE
#include "tree.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Helper: case-insensitive substring search                           */
/* ------------------------------------------------------------------ */

static int str_icontains(const char *haystack, const char *needle)
{
    if (!haystack || !needle || needle[0] == '\0') return 1;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return 0;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) break;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* tree_build                                                          */
/* ------------------------------------------------------------------ */

ProcessTree *tree_build(Process **procs, int count)
{
    ProcessTree *t = calloc(1, sizeof(ProcessTree));
    if (!t) return NULL;

    t->all   = procs;
    t->count = count;

    /* Populate hash map: direct indexing by PID */
    for (int i = 0; i < count; i++) {
        Process *p = procs[i];
        if (p->pid > 0 && p->pid < MAX_PROCESSES) {
            t->hash[p->pid] = p;
        }
    }

    /* Link parent → child relationships */
    for (int i = 0; i < count; i++) {
        Process *p = procs[i];
        if (p->pid == 1) {
            t->root = p;
            continue;
        }
        if (p->ppid > 0 && p->ppid < MAX_PROCESSES) {
            Process *parent = t->hash[p->ppid];
            if (parent) {
                process_add_child(parent, p);
                p->parent = parent;
                continue;
            }
        }
        /* No parent found — will attach to root after */
    }

    /* Attach orphans to root (PID 1) */
    if (t->root) {
        for (int i = 0; i < count; i++) {
            Process *p = procs[i];
            if (p->pid == 1) continue;
            if (p->parent == NULL) {
                process_add_child(t->root, p);
            }
        }
    }

    return t;
}

void tree_free(ProcessTree *t)
{
    if (!t) return;
    for (int i = 0; i < t->count; i++) {
        process_free(t->all[i]);
    }
    free(t->all);
    free(t);
}

/* ------------------------------------------------------------------ */
/* DFS traversal                                                       */
/* ------------------------------------------------------------------ */

static void dfs_helper(Process *p, int depth, TreeVisitor visitor, void *data)
{
    if (!p) return;
    visitor(p, depth, data);
    if (p->collapsed) return;  /* don't descend into collapsed subtrees */
    for (int i = 0; i < p->child_count; i++) {
        dfs_helper(p->children[i], depth + 1, visitor, data);
    }
}

void tree_walk_dfs(ProcessTree *t, TreeVisitor visitor, void *data)
{
    if (!t || !visitor || !t->root) return;
    dfs_helper(t->root, 0, visitor, data);
}

void tree_walk_ancestry(Process *p, TreeVisitor visitor, void *data)
{
    if (!p || !visitor) return;
    /* Build ancestry chain (p … root), then reverse-visit */
    Process *chain[4096];
    int      depth = 0;
    Process *cur   = p;
    while (cur && depth < 4096) {
        chain[depth++] = cur;
        cur = cur->parent;
    }
    /* Visit from root down to p */
    for (int i = depth - 1; i >= 0; i--) {
        visitor(chain[i], depth - 1 - i, data);
    }
}

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

int tree_search(ProcessTree *t, const char *query)
{
    if (!t) return 0;

    int matches = 0;
    int has_query = (query && query[0] != '\0');

    /* Check if query is purely numeric (PID search) */
    int is_pid_query = 0;
    int target_pid   = 0;
    if (has_query) {
        is_pid_query = 1;
        for (int i = 0; query[i]; i++) {
            if (!isdigit((unsigned char)query[i])) { is_pid_query = 0; break; }
        }
        if (is_pid_query) target_pid = atoi(query);
    }

    for (int i = 0; i < t->count; i++) {
        Process *p = t->all[i];
        p->highlighted = 0;

        if (!has_query) continue;

        int match = 0;
        if (is_pid_query) {
            match = (p->pid == target_pid);
        } else {
            match = str_icontains(p->name, query) ||
                    str_icontains(p->cmdline, query) ||
                    str_icontains(p->username, query);
        }

        if (match) {
            p->highlighted = 1;
            matches++;
        }
    }

    return matches;
}

Process *tree_find_pid(ProcessTree *t, int pid)
{
    if (!t || pid <= 0 || pid >= MAX_PROCESSES) return NULL;
    return t->hash[pid];
}

/* ------------------------------------------------------------------ */
/* Statistics                                                          */
/* ------------------------------------------------------------------ */

int tree_count_zombies(ProcessTree *t)
{
    int n = 0;
    for (int i = 0; i < t->count; i++) {
        if (t->all[i]->is_zombie) n++;
    }
    return n;
}

int tree_count_reparented(ProcessTree *t)
{
    int n = 0;
    for (int i = 0; i < t->count; i++) {
        if (t->all[i]->is_reparented) n++;
    }
    return n;
}

int tree_count_new(ProcessTree *t)
{
    int n = 0;
    for (int i = 0; i < t->count; i++) {
        if (t->all[i]->is_new) n++;
    }
    return n;
}
