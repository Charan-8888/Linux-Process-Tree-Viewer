/**
 * tree.h — Process tree builder, traversal, and search.
 *
 * Takes a flat array of Process records (from proc_reader) and builds the
 * parent-child hierarchy using PID/PPID matching.  Provides DFS traversal
 * and incremental text search.
 */
#ifndef TREE_H
#define TREE_H

#include "process.h"

/* Maximum number of processes we support in one snapshot */
#define MAX_PROCESSES 65536

/**
 * ProcessTree — owns a flat table of processes and the root of the tree.
 */
typedef struct {
    Process **all;          /* flat array, indexed 0..count-1        */
    int       count;        /* number of entries                     */
    Process  *root;         /* process with pid==1 (or virtual root) */

    /* Hash map: pid → Process* for O(1) lookup */
    Process  *hash[MAX_PROCESSES];
} ProcessTree;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/**
 * Build a ProcessTree from a flat array of Process pointers.
 * The tree takes ownership of the array and its elements.
 *
 * @param procs     Flat array returned by proc_read_all()
 * @param count     Number of elements
 * @return          Heap-allocated ProcessTree, or NULL on error.
 */
ProcessTree *tree_build(Process **procs, int count);

/**
 * Free a ProcessTree and all Process nodes it owns.
 */
void tree_free(ProcessTree *t);

/* ------------------------------------------------------------------ */
/* Traversal                                                           */
/* ------------------------------------------------------------------ */

/**
 * Callback type for tree_walk_dfs().
 * @param p       Current node
 * @param depth   Depth from root (root = 0)
 * @param data    User-supplied context pointer
 */
typedef void (*TreeVisitor)(Process *p, int depth, void *data);

/**
 * DFS traversal starting from root.
 * Skips collapsed subtrees (p->collapsed != 0).
 */
void tree_walk_dfs(ProcessTree *t, TreeVisitor visitor, void *data);

/**
 * Walk the ancestry chain from p back to the root.
 * Calls visitor for each ancestor including p itself.
 */
void tree_walk_ancestry(Process *p, TreeVisitor visitor, void *data);

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

/**
 * Mark processes matching 'query' (name, PID, cmdline, username).
 * Sets p->highlighted = 1 for matches, 0 otherwise.
 * A NULL or empty query clears all highlights.
 *
 * @return number of matches found.
 */
int tree_search(ProcessTree *t, const char *query);

/**
 * Look up a process by PID.  O(1) via hash map.
 */
Process *tree_find_pid(ProcessTree *t, int pid);

/* ------------------------------------------------------------------ */
/* Statistics                                                          */
/* ------------------------------------------------------------------ */

/** Count zombie processes. */
int tree_count_zombies(ProcessTree *t);

/** Count re-parented processes. */
int tree_count_reparented(ProcessTree *t);

/** Count newly created processes (is_new == 1). */
int tree_count_new(ProcessTree *t);

#endif /* TREE_H */
