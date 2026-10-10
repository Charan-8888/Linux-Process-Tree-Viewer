# PTVA Architecture Reference

## Module Map

```
┌─────────────────────────────────────────────────────────────┐
│                    main.c  (entry point)                    │
│  parse args → monitor_start() → ui_run() → monitor_stop()  │
└───────────────────────────┬─────────────────────────────────┘
                            │
          ┌─────────────────▼──────────────────┐
          │         monitor.c / monitor.h       │
          │  pthread refresh loop               │
          │  event ring buffer                  │
          │  snapshot store                     │
          │  sys cpu/mem update                 │
          └──┬──────────┬──────────┬────────────┘
             │          │          │
   ┌──────── ▼──┐  ┌────▼───┐  ┌──▼──────────┐
   │ proc_reader│  │  tree  │  │  analyzer   │
   │ /proc scan │  │ build  │  │ cpu%        │
   │ stat parse │  │ DFS    │  │ zombie chk  │
   │ cmdline    │  │ search │  │ reparent    │
   └──────┬─────┘  └────────┘  └─────────────┘
          │
   ┌──────▼─────┐
   │  process   │
   │  struct    │
   │  helpers   │
   └────────────┘

          ┌─────────────────┐
          │   ui.c / ui.h   │
          │ ncurses TUI     │
          │ tree panel      │
          │ detail panel    │
          │ events panel    │
          │ search bar      │
          │ status bar      │
          └─────────────────┘
```

## Data Flow (one refresh cycle)

```
1. refresh_thread wakes up (every interval_ms)
2. proc_read_all()
   └─ opendir(/proc) → iterate numeric dirs
   └─ proc_read_one(pid):
      ├─ parse /proc/<pid>/stat   → pid,ppid,name,state,utime,stime
      ├─ parse /proc/<pid>/status → VmRSS, Uid, Threads
      └─ parse /proc/<pid>/cmdline

3. tree_build(procs, count)
   └─ fill hash[pid] = process*
   └─ for each process: find parent by ppid, add as child
   └─ orphans attached to PID 1

4. analyzer_compute_cpu(new_tree, cpu_state)
   └─ Δ(utime+stime) / Δ(system total) × 100

5. analyzer_detect_reparenting(old_tree, new_tree)
   └─ compare current.ppid vs previous.ppid → set is_reparented

6. diff_trees(old, new)
   └─ push events: CREATED, TERMINATED, ZOMBIE, REPARENTED

7. mutex lock → swap m->tree = new_tree → mutex unlock
8. free old_tree

UI thread (main):
9.  monitor_lock_tree() → rebuild_visible() → draw panels
10. getch() → handle keyboard → monitor_unlock()
```

## Thread Safety

- All shared state lives in `Monitor`.
- The refresh thread (`refresh_thread`) and the UI (main thread) share
  `Monitor.tree` and the event ring buffer.
- A single `pthread_mutex_t` (`Monitor._mutex`) protects all shared fields.
- The UI holds the lock only during `rebuild_visible()` and drawing (< 1 ms).
- The refresh thread holds the lock only during the tree swap (< 1 ms).

## Memory Ownership

| Owner | What it owns |
|---|---|
| `ProcessTree` | `all[]` array + every `Process*` in it |
| `tree_free()` | frees all `Process` nodes, then `all[]`, then `ProcessTree` |
| `Monitor` | current `tree`, all `snapshots[]`, `cpu_state` |
| `monitor_stop()` | frees everything owned by `Monitor` |

## Hash Map

- Direct index: `hash[pid]` for `0 < pid < MAX_PROCESSES` (65 536).
- Linux default PID_MAX is 32 768; kernel can be configured up to 4 194 304.
- Processes with PID ≥ MAX_PROCESSES are stored in the flat array but not
  in the hash map (ancestry and search still work via linear scan in those
  rare cases).

## CPU% Calculation

```
Δprocess = (utime_now + stime_now) - (utime_prev + stime_prev)
Δsystem  = total_jiffies_now - total_jiffies_prev    (/proc/stat cpu line)
cpu%     = Δprocess / Δsystem × 100
```

Jiffies are integer clock ticks; the formula gives a percentage in
`[0, 100]` per process per refresh interval.

## Event Ring Buffer

- Fixed-size circular array of `EVENT_RING_SIZE = 256` `ProcessEvent` records.
- New events overwrite the oldest.
- `monitor_get_events()` copies the N most-recent events (most recent first).
- Protected by the same global mutex.

## Snapshot Comparison

1. `monitor_save_snapshot()` deep-clones the current tree (copies all
   `Process` scalar fields; rebuilds tree links).
2. `monitor_compare_snapshots(a, b)` calls `diff_trees(a, b)` which
   pushes created/terminated events to the ring buffer.
3. The UI key `c` compares the last two saved snapshots.
