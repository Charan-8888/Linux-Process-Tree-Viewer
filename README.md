# Process Tree Viewer & Analyzer (PTVA)

> An educational Linux systems-programming tool that makes process management **visible**.

PTVA reconstructs the parent-child process hierarchy from the Linux `/proc` pseudo-filesystem, monitors how it changes over time, and analyses process states, resource usage, zombie conditions, re-parenting events, and lifecycle events — all inside a live ncurses TUI.

---

## Screenshot (TUI Layout)

```
  ⬢ PROCESS TREE VIEWER & ANALYZER  [PTVA]              v1.0
 Procs: 247  | CPU: 24.1% | RAM: 61.3% | Zombies: 1 | LIVE ◆
 Filter: python  (Esc to clear)
┌─────────────────────────────────┐┌──────────────────────────┐
│ PROCESS TREE                    ││ PROCESS DETAILS          │
│ systemd [1] S                   ││ Name        python        │
│ ├── NetworkManager [734] S      ││ PID         5100          │
│ ├── sshd [1200] S               ││ PPID        4210          │
│ │   └── bash [4210] S           ││ State       R  Running    │
│ │       ├── python [5100] R ★   ││ User        charan        │
│ │       └── gcc [5200] R        ││ CPU         12.34%        │
│ └── nginx [1500] S              ││ Memory      182 KB        │
│     ├── nginx [1501] S          ││ Threads     8             │
│     └── nginx [1502] S          ││ Children    2             │
│                                 ││ Parent      bash [4210]   │
│                                 │├──────────────────────────┤
│                                 ││ EVENTS                   │
│                                 ││ + 12:20:01 [5101] worker │
│                                 ││ + 12:20:02 [5102] worker │
│                                 ││ - 12:20:03 [5001] gcc    │
│                                 ││ Z 12:20:04 [4821] app    │
└─────────────────────────────────┘└──────────────────────────┘
```

---

## Features

| Feature | Status |
|---|---|
| Live process tree from `/proc` | ✅ |
| PID / PPID tree construction | ✅ |
| Process state display (R S D T Z I) | ✅ |
| CPU% per process (sampled) | ✅ |
| Memory (RSS) per process | ✅ |
| Thread count | ✅ |
| Username resolution | ✅ |
| Zombie detection & highlight | ✅ |
| Re-parenting detection | ✅ |
| Process creation/termination events | ✅ |
| Named snapshot save | ✅ |
| Snapshot comparison diff | ✅ |
| Incremental search (name/PID/cmd/user) | ✅ |
| Expand/collapse subtrees | ✅ |
| Ancestry chain view | ✅ |
| Configurable refresh interval | ✅ |
| System-wide CPU & RAM status bar | ✅ |

---

## Build

### Prerequisites (Linux only)

```bash
# Debian / Ubuntu
sudo apt install gcc make libncurses-dev

# Fedora / RHEL / CentOS
sudo dnf install gcc make ncurses-devel
```

### Compile

```bash
cd process-tree-viewer
make          # build ptva binary
make tests    # build test programs
make all      # build everything
```

### Run

```bash
./ptva                # default 1-second refresh
./ptva -i 500         # 500 ms refresh
./ptva -h             # help
```

---

## Keyboard Controls

| Key | Action |
|---|---|
| `↑` / `↓` (or `k`/`j`) | Navigate tree |
| `Enter` / `e` | Expand or collapse subtree |
| `/` | Enter search mode |
| `Esc` | Clear search |
| `a` | Toggle ancestry chain view |
| `s` | Save named snapshot |
| `c` | Compare last two snapshots |
| `+` / `-` | Decrease / increase refresh interval by 200 ms |
| `Page Up/Down` | Scroll by half-page |
| `Home` / `End` | Jump to first / last process |
| `q` | Quit |

---

## Demo Test Programs

### Zombie Demo

```bash
cd tests
gcc -Wall -std=c11 test_fork_zombie.c -o test_fork_zombie
./test_fork_zombie &
# In another terminal, run ptva and watch the zombie (state Z)
```

### Re-parenting Demo

```bash
gcc -Wall -std=c11 test_reparent.c -o test_reparent
./test_reparent &
# In ptva, look for the grandchild's PPID changing to 1
```

---

## Architecture

```
LINUX /proc
     │
     ▼
proc_reader  ─────────────────────────────────┐
  Scans /proc/*                               │
  Parses stat / status / cmdline              │
     │                                        │
     ▼                                        │
process model                                 │
  (Process struct)                            │
     │                                        │
     ▼                                        │
tree builder                                  │
  Hash map PID→Process                        │
  Link child.ppid → parent.pid               │
     │                                        │
     ▼                                        │
analyzer                                      │
  CPU% delta                                  │
  Zombie check                                │
  Re-parenting check                          │
     │                                        │
     ▼                                        │
monitor (pthread)       ◄───────────────────── periodic refresh
  Double-buffer swap
  Event ring buffer
  Snapshot store
     │
     ▼
  UI (ncurses)
  Tree panel | Detail panel | Events panel
```

---

## OS Concepts Demonstrated

| OS Concept | Where |
|---|---|
| Process / PID / PPID | Core data model |
| `/proc` filesystem | `proc_reader.c` |
| Process states (R S D T Z I) | Display & color coding |
| `fork()` | `test_fork_zombie.c`, `test_reparent.c` |
| `wait()` / zombie | `test_fork_zombie.c` + zombie detection |
| Orphan / re-parenting | `test_reparent.c` + re-parent analysis |
| CPU scheduling | CPU% computation |
| Memory management | RSS display |
| Signals | Implicit via process lifecycle |
| Tree data structure | `tree.c` |

---

## Project Structure

```
process-tree-viewer/
├── Makefile
├── README.md
├── src/
│   ├── main.c          Entry point & argument parsing
│   ├── process.h/.c    Process data model
│   ├── proc_reader.h/.c /proc scanner & parser
│   ├── tree.h/.c       Tree builder, traversal, search
│   ├── analyzer.h/.c   Zombie, re-parent, CPU/mem analysis
│   ├── monitor.h/.c    Refresh thread, events, snapshots
│   └── ui.h/.c         ncurses TUI
├── tests/
│   ├── test_fork_zombie.c
│   └── test_reparent.c
└── docs/
    └── architecture.md
```

---

## Limitations

- **Linux only** — requires the `/proc` pseudo-filesystem.
- Some process information (e.g., for processes owned by other users) may be restricted by kernel permissions.
- CPU% is computed from sampled counters and may be 0 in the first refresh cycle.
- A process that terminates mid-scan is silently skipped (no crash).

---

## License

Educational project — Operating Systems course.
