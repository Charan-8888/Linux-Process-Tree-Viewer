/**
 * proc_reader.h — /proc filesystem scanner and parser.
 *
 * Scans /proc, discovers all numeric PID directories, reads per-process
 * information, and returns a flat array of Process records. The caller
 * is responsible for freeing the returned array.
 */
#ifndef PROC_READER_H
#define PROC_READER_H

#include "process.h"

/**
 * Scan /proc and populate an array of Process records.
 *
 * @param out_count  Receives the number of processes found.
 * @return           Heap-allocated array of Process*, caller must free
 *                   each element and the array itself.
 *                   Returns NULL on failure.
 */
Process **proc_read_all(int *out_count);

/**
 * Read a single process from /proc/<pid>.
 * Returns NULL if the process has already disappeared or cannot be read.
 */
Process *proc_read_one(int pid);

/**
 * Read system-wide CPU time from /proc/stat.
 * Fills total_time and idle_time in clock ticks.
 */
int proc_read_system_cpu(unsigned long *total_time, unsigned long *idle_time);

/**
 * Read system-wide memory info from /proc/meminfo.
 * Fills total_kb and available_kb in kilobytes.
 */
int proc_read_system_mem(long *total_kb, long *available_kb);

#endif /* PROC_READER_H */
