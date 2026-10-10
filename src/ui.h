/**
 * ui.h — ncurses TUI for Process Tree Viewer & Analyzer.
 *
 * Layout:
 *   ┌──────────────────────────────────────────────────┐
 *   │ PROCESS TREE VIEWER & ANALYZER          [header] │
 *   │ Processes: 247 | CPU: 24% | RAM: 61% | LIVE ●   │
 *   ├──────────────────────────────────────────────────┤
 *   │ Search: [___________]                  [search]  │
 *   ├──────────────────────────┬───────────────────────┤
 *   │ PROCESS TREE             │ PROCESS DETAILS       │
 *   │ (scrollable DFS tree)    │ (selected process)    │
 *   │                          ├───────────────────────┤
 *   │                          │ EVENTS                │
 *   │                          │ (ring buffer)         │
 *   └──────────────────────────┴───────────────────────┘
 *
 * Keyboard:
 *   ↑ ↓           Navigate tree
 *   Enter         Select process / open details
 *   e             Expand / Collapse subtree
 *   /             Enter search mode
 *   Esc           Clear search
 *   s             Save snapshot
 *   c             Compare last two snapshots
 *   a             Show ancestry for selected process
 *   q             Quit
 *   +/-           Increase / decrease refresh interval
 */
#ifndef UI_H
#define UI_H

#include "monitor.h"

/**
 * Initialise ncurses, start the main event loop, and clean up on exit.
 * Does not return until the user quits.
 */
void ui_run(Monitor *m);

#endif /* UI_H */
