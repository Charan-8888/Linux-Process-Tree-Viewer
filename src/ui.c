/**
 * ui.c — ncurses TUI for Process Tree Viewer & Analyzer.
 *
 * Uses ncurses wide-character (ncursesw) for box-drawing characters
 * (├ └ │ ─).  Falls back gracefully if color is unavailable.
 *
 * This is a single-threaded UI that calls monitor_lock_tree() to obtain
 * a consistent snapshot, renders it, then releases the lock quickly.
 */
#define _DEFAULT_SOURCE
#include "ui.h"
#include "tree.h"
#include "analyzer.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Color pairs                                                         */
/* ------------------------------------------------------------------ */
#define CP_HEADER       1   /* white on dark blue  */
#define CP_STATUS       2   /* black on cyan       */
#define CP_TREE_NORMAL  3   /* normal              */
#define CP_TREE_SELECT  4   /* black on yellow     */
#define CP_TREE_ZOMBIE  5   /* white on red        */
#define CP_TREE_RUNNING 6   /* bright green        */
#define CP_TREE_SLEEP   7   /* cyan                */
#define CP_TREE_STOP    8   /* magenta             */
#define CP_TREE_SEARCH  9   /* black on green      */
#define CP_DETAIL_KEY   10  /* bright cyan         */
#define CP_BORDER       11  /* dark blue           */
#define CP_EVENT_CREATE 12  /* bright green        */
#define CP_EVENT_TERM   13  /* red                 */
#define CP_EVENT_ZOMBIE 14  /* white on red        */
#define CP_EVENT_REPRT  15  /* bright yellow       */
#define CP_REPARENTED   16  /* yellow              */

/* ------------------------------------------------------------------ */
/* Flat node list for the visible tree                                 */
/* ------------------------------------------------------------------ */
#define MAX_VISIBLE 8192

typedef struct {
    Process *proc;
    int      depth;
    int      is_last_child;  /* true → use └── instead of ├── */
    char     prefix[512];    /* inherited box-drawing prefix   */
} VisibleNode;

/* ------------------------------------------------------------------ */
/* UI State                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    Monitor  *mon;

    /* Tree panel */
    WINDOW   *win_tree;
    WINDOW   *win_detail;
    WINDOW   *win_events;
    WINDOW   *win_status;
    WINDOW   *win_search;

    /* Visible node list (rebuilt each frame) */
    VisibleNode vnodes[MAX_VISIBLE];
    int         vcount;

    /* Selection */
    int      sel_idx;       /* index in vnodes[]  */
    int      scroll_top;    /* first visible row  */

    /* Search */
    char     search_buf[256];
    int      search_mode;   /* 1 = typing         */

    /* Ancestry mode */
    int      ancestry_mode;

    int      tree_rows;     /* usable rows in tree panel */
    int      tree_cols;
    int      detail_cols;
} UIState;

/* ------------------------------------------------------------------ */
/* Build visible node list (DFS, respect collapsed)                   */
/* ------------------------------------------------------------------ */

typedef struct {
    UIState    *ui;
    int         idx;
} CollectCtx;

static void collect_node(Process *p, int depth, void *data)
{
    CollectCtx *ctx = (CollectCtx *)data;
    if (ctx->idx >= MAX_VISIBLE) return;

    VisibleNode *vn = &ctx->ui->vnodes[ctx->idx++];
    vn->proc  = p;
    vn->depth = depth;
    vn->is_last_child = (p->parent == NULL) ? 1 :
        (p == p->parent->children[p->parent->child_count - 1]);

    /* Build prefix string */
    vn->prefix[0] = '\0';
    if (depth == 0) return;

    /* Walk up the parent chain to build the vertical bars */
    Process *ancestors[256];
    int anc_count = 0;
    Process *cur = p->parent;
    while (cur && cur->parent && anc_count < 255) {
        ancestors[anc_count++] = cur;
        cur = cur->parent;
    }
    /* ancestors[0] = immediate parent, ancestors[anc_count-1] = child of root */
    /* Build prefix from root side */
    char buf[512] = "";
    for (int i = anc_count - 1; i >= 0; i--) {
        Process *anc = ancestors[i];
        int is_last = (anc->parent == NULL) ? 1 :
            (anc == anc->parent->children[anc->parent->child_count - 1]);
        if (is_last) strncat(buf, "   ", sizeof(buf) - strlen(buf) - 1);
        else         strncat(buf, "|  ", sizeof(buf) - strlen(buf) - 1); /* │ */
    }
    strncpy(vn->prefix, buf, sizeof(vn->prefix) - 1);
}

static void rebuild_visible(UIState *ui, ProcessTree *t)
{
    ui->vcount = 0;
    if (!t) return;

    CollectCtx ctx = { ui, 0 };
    tree_walk_dfs(t, collect_node, &ctx);
    ui->vcount = ctx.idx;

    /* Clamp selection */
    if (ui->sel_idx >= ui->vcount) ui->sel_idx = ui->vcount - 1;
    if (ui->sel_idx < 0) ui->sel_idx = 0;
}

/* ------------------------------------------------------------------ */
/* Draw helpers                                                        */
/* ------------------------------------------------------------------ */

static void draw_hline(WINDOW *w, int y, int x, int len)
{
    wmove(w, y, x);
    for (int i = 0; i < len; i++) waddch(w, ACS_HLINE);
}

/* Truncate string to at most 'max' bytes, add '…' if needed */
static void trunc_str(char *dst, const char *src, int max)
{
    if (max <= 0) { dst[0] = '\0'; return; }
    int len = (int)strlen(src);
    if (len <= max) {
        strncpy(dst, src, (size_t)max);
        dst[max] = '\0';
    } else {
        strncpy(dst, src, (size_t)(max - 1));
        dst[max - 1] = '\0';
        /* append ellipsis */
        if (max > 1) strcat(dst, "…");
    }
}

/* ------------------------------------------------------------------ */
/* Draw: header / status bar                                           */
/* ------------------------------------------------------------------ */

static void draw_status(UIState *ui, ProcessTree *t, Monitor *mon)
{
    WINDOW *w = ui->win_status;
    werase(w);
    wbkgd(w, COLOR_PAIR(CP_STATUS) | A_BOLD);

    int count = t ? t->count : 0;
    int zombies = t ? tree_count_zombies(t) : 0;

    /* Lock not needed — we already hold the lock (called from draw frame) */
    mvwprintw(w, 0, 1,
              " Procs: %-4d | CPU: %4.1f%% | RAM: %4.1f%% | Zombies: %d | LIVE",
              count,
              mon->sys_cpu_percent,
              mon->sys_mem_percent,
              zombies);

    /* Blinking dot */
    int cols = getmaxx(w);
    wattron(w, A_BLINK | COLOR_PAIR(CP_EVENT_CREATE));
    mvwaddch(w, 0, cols - 3, ACS_DIAMOND);
    wattroff(w, A_BLINK | COLOR_PAIR(CP_EVENT_CREATE));

    wrefresh(w);
}

/* ------------------------------------------------------------------ */
/* Draw: search bar                                                    */
/* ------------------------------------------------------------------ */

static void draw_search(UIState *ui)
{
    WINDOW *w = ui->win_search;
    werase(w);
    wbkgd(w, COLOR_PAIR(CP_TREE_NORMAL));

    if (ui->search_mode) {
        wattron(w, A_BOLD | COLOR_PAIR(CP_DETAIL_KEY));
        mvwaddstr(w, 0, 1, "Search: ");
        wattroff(w, A_BOLD | COLOR_PAIR(CP_DETAIL_KEY));
        waddstr(w, ui->search_buf);
        waddch(w, '_');
    } else if (ui->search_buf[0]) {
        wattron(w, COLOR_PAIR(CP_DETAIL_KEY));
        mvwprintw(w, 0, 1, "Filter: %s  (Esc to clear)", ui->search_buf);
        wattroff(w, COLOR_PAIR(CP_DETAIL_KEY));
    } else {
        wattron(w, A_DIM);
        mvwaddstr(w, 0, 1,
                  "/ search  ↑↓ navigate  Enter select  e expand  s snapshot  a ancestry  q quit");
        wattroff(w, A_DIM);
    }
    wrefresh(w);
}

/* ------------------------------------------------------------------ */
/* Draw: process tree panel                                            */
/* ------------------------------------------------------------------ */

static void draw_tree(UIState *ui)
{
    WINDOW *w = ui->win_tree;
    werase(w);
    wbkgd(w, COLOR_PAIR(CP_TREE_NORMAL));

    /* Title */
    wattron(w, A_BOLD | COLOR_PAIR(CP_DETAIL_KEY));
    mvwaddstr(w, 0, 1, " PROCESS TREE ");
    wattroff(w, A_BOLD | COLOR_PAIR(CP_DETAIL_KEY));
    draw_hline(w, 1, 0, getmaxx(w));

    int rows_avail = ui->tree_rows - 2;  /* below title+hline */
    int cols = ui->tree_cols;

    /* Adjust scroll so selection stays visible */
    if (ui->sel_idx < ui->scroll_top) {
        ui->scroll_top = ui->sel_idx;
    }
    if (ui->sel_idx >= ui->scroll_top + rows_avail) {
        ui->scroll_top = ui->sel_idx - rows_avail + 1;
    }
    if (ui->scroll_top < 0) ui->scroll_top = 0;

    for (int i = 0; i < rows_avail; i++) {
        int vi = ui->scroll_top + i;
        if (vi >= ui->vcount) break;

        VisibleNode *vn = &ui->vnodes[vi];
        Process     *p  = vn->proc;
        int row = i + 2;

        /* Choose connector */
        const char *connector = vn->is_last_child ?
            "\\-- " :   /* └── */
            "|-- ";     /* ├── */

        /* Choose color based on state */
        int pair;
        if (vi == ui->sel_idx) {
            pair = CP_TREE_SELECT;
        } else if (p->highlighted && ui->search_buf[0]) {
            pair = CP_TREE_SEARCH;
        } else if (p->is_zombie) {
            pair = CP_TREE_ZOMBIE;
        } else if (p->is_reparented) {
            pair = CP_REPARENTED;
        } else {
            switch (p->state) {
                case STATE_RUNNING:    pair = CP_TREE_RUNNING; break;
                case STATE_SLEEPING:   pair = CP_TREE_SLEEP;   break;
                case STATE_STOPPED:    pair = CP_TREE_STOP;    break;
                default:               pair = CP_TREE_NORMAL;  break;
            }
        }

        wattron(w, COLOR_PAIR(pair));
        if (vi == ui->sel_idx) wattron(w, A_BOLD | A_REVERSE);

        /* Clear line */
        wmove(w, row, 0);
        for (int c = 0; c < cols; c++) waddch(w, ' ');

        /* Build display string */
        char line[512];
        char name_state[320];

        /* State indicator */
        char state_icon = p->state;
        if (p->is_zombie)     state_icon = 'Z';
        if (p->is_reparented) state_icon = 'R';

        /* Collapse indicator */
        const char *coll_str = (p->collapsed && p->child_count > 0) ? "+" : " ";

        snprintf(name_state, sizeof(name_state),
                 "%s [%d] %c  %.1f%%  %ldMB",
                 p->name,
                 p->pid,
                 state_icon,
                 p->cpu_usage,
                 p->vm_rss_kb / 1024);

        if (vn->depth == 0) {
            snprintf(line, sizeof(line), "%s%s%s",
                     coll_str, vn->prefix, name_state);
        } else {
            snprintf(line, sizeof(line), "%s%s%s%s",
                     coll_str, vn->prefix, connector, name_state);
        }

        /* Truncate to available width */
        char display[512];
        trunc_str(display, line, cols - 1);
        mvwaddstr(w, row, 0, display);

        wattroff(w, COLOR_PAIR(pair) | A_BOLD | A_REVERSE);
    }

    /* Scrollbar indicator */
    if (ui->vcount > rows_avail) {
        int bar_pos = (ui->scroll_top * rows_avail) / ui->vcount + 2;
        wattron(w, COLOR_PAIR(CP_BORDER));
        mvwaddch(w, bar_pos, cols - 1, ACS_BLOCK);
        wattroff(w, COLOR_PAIR(CP_BORDER));
    }

    box(w, 0, 0);
    wrefresh(w);
}

/* ------------------------------------------------------------------ */
/* Draw: process detail panel                                          */
/* ------------------------------------------------------------------ */

static void draw_detail(UIState *ui)
{
    WINDOW *w = ui->win_detail;
    werase(w);
    wbkgd(w, COLOR_PAIR(CP_TREE_NORMAL));
    box(w, 0, 0);

    wattron(w, A_BOLD | COLOR_PAIR(CP_DETAIL_KEY));
    mvwaddstr(w, 0, 1, " PROCESS DETAILS ");
    wattroff(w, A_BOLD | COLOR_PAIR(CP_DETAIL_KEY));

    if (ui->ancestry_mode) {
        /* Show ancestry chain */
        wattron(w, A_BOLD | COLOR_PAIR(CP_EVENT_REPRT));
        mvwaddstr(w, 1, 2, "ANCESTRY CHAIN:");
        wattroff(w, A_BOLD | COLOR_PAIR(CP_EVENT_REPRT));
    }

    if (ui->sel_idx < 0 || ui->sel_idx >= ui->vcount) {
        mvwaddstr(w, 2, 2, "(no selection)");
        wrefresh(w);
        return;
    }

    Process *p = ui->vnodes[ui->sel_idx].proc;
    if (!p) { wrefresh(w); return; }

    if (ui->ancestry_mode) {
        /* Print ancestry chain */
        int row = 2;
        Process *cur = p;
        while (cur && row < getmaxy(w) - 1) {
            wattron(w, COLOR_PAIR(CP_DETAIL_KEY));
            mvwprintw(w, row, 2, "%d", cur->pid);
            wattroff(w, COLOR_PAIR(CP_DETAIL_KEY));
            mvwprintw(w, row, 10, "%s", cur->name);
            if (cur->parent) {
                wattron(w, COLOR_PAIR(CP_BORDER));
                mvwaddstr(w, row + 1, 3, "^"); /* ↑ */
                wattroff(w, COLOR_PAIR(CP_BORDER));
                row++;
            }
            row++;
            cur = cur->parent;
        }
        wrefresh(w);
        return;
    }

    /* Normal detail view */
#define DROW(label, fmt, ...) do { \
    wattron(w, COLOR_PAIR(CP_DETAIL_KEY) | A_BOLD); \
    mvwprintw(w, row, 2, "%-12s", label); \
    wattroff(w, COLOR_PAIR(CP_DETAIL_KEY) | A_BOLD); \
    wprintw(w, fmt, __VA_ARGS__); \
    row++; \
} while (0)

    int row = 1;
    DROW("Name",     "%s",   p->name);
    DROW("PID",      "%d",   p->pid);
    DROW("PPID",     "%d",   p->ppid);
    DROW("State",    "%c  %s", p->state, process_state_str(p->state));
    DROW("User",     "%s",   p->username[0] ? p->username : "?");
    DROW("CPU",      "%.2f%%", p->cpu_usage);
    DROW("Memory",   "%ld KB (%ld MB)", p->vm_rss_kb, p->vm_rss_kb / 1024);
    DROW("Threads",  "%d",   p->threads);
    DROW("Children", "%d",   p->child_count);

    /* Parent name */
    const char *par_name = p->parent ? p->parent->name : "(none)";
    wattron(w, COLOR_PAIR(CP_DETAIL_KEY) | A_BOLD);
    mvwprintw(w, row, 2, "%-12s", "Parent");
    wattroff(w, COLOR_PAIR(CP_DETAIL_KEY) | A_BOLD);
    wprintw(w, "%s [%d]", par_name, p->ppid);
    row++;

    /* Command line */
    row++;
    wattron(w, COLOR_PAIR(CP_DETAIL_KEY) | A_BOLD);
    mvwaddstr(w, row++, 2, "Command:");
    wattroff(w, COLOR_PAIR(CP_DETAIL_KEY) | A_BOLD);
    char cmd[64];
    trunc_str(cmd, p->cmdline, getmaxx(w) - 4);
    mvwaddstr(w, row++, 3, cmd);

    /* Flags */
    if (p->is_zombie) {
        wattron(w, COLOR_PAIR(CP_TREE_ZOMBIE) | A_BOLD | A_BLINK);
        mvwaddstr(w, row++, 2, "⚠ ZOMBIE PROCESS");
        wattroff(w, COLOR_PAIR(CP_TREE_ZOMBIE) | A_BOLD | A_BLINK);
    }
    if (p->is_reparented) {
        wattron(w, COLOR_PAIR(CP_REPARENTED) | A_BOLD);
        mvwprintw(w, row++, 2, "↺ REPARENTED (was %d)", p->prev_ppid);
        wattroff(w, COLOR_PAIR(CP_REPARENTED) | A_BOLD);
    }
    if (p->is_new) {
        wattron(w, COLOR_PAIR(CP_EVENT_CREATE) | A_BOLD);
        mvwaddstr(w, row++, 2, "★ NEW PROCESS");
        wattroff(w, COLOR_PAIR(CP_EVENT_CREATE) | A_BOLD);
    }

    /* Keyboard hints at bottom */
    int drows = getmaxy(w);
    wattron(w, A_DIM);
    mvwaddstr(w, drows - 2, 2, "a=ancestry  e=expand/collapse");
    wattroff(w, A_DIM);

#undef DROW
    wrefresh(w);
}

/* ------------------------------------------------------------------ */
/* Draw: events panel                                                  */
/* ------------------------------------------------------------------ */

static void draw_events(UIState *ui)
{
    WINDOW *w = ui->win_events;
    werase(w);
    wbkgd(w, COLOR_PAIR(CP_TREE_NORMAL));
    box(w, 0, 0);

    wattron(w, A_BOLD | COLOR_PAIR(CP_DETAIL_KEY));
    mvwaddstr(w, 0, 1, " EVENTS ");
    wattroff(w, A_BOLD | COLOR_PAIR(CP_DETAIL_KEY));

    ProcessEvent evs[32];
    int n = monitor_get_events(ui->mon, evs, 32);

    int rows_avail = getmaxy(w) - 2;
    int show = (n < rows_avail) ? n : rows_avail;
    int cols = getmaxx(w) - 3;

    for (int i = 0; i < show; i++) {
        ProcessEvent *ev = &evs[i];
        int row = 1 + i;
        int pair;
        char sym;
        switch (ev->type) {
            case EVENT_CREATED:    pair = CP_EVENT_CREATE; sym = '+'; break;
            case EVENT_TERMINATED: pair = CP_EVENT_TERM;   sym = '-'; break;
            case EVENT_ZOMBIE:     pair = CP_EVENT_ZOMBIE; sym = 'Z'; break;
            case EVENT_REPARENTED: pair = CP_EVENT_REPRT;  sym = 'R'; break;
            default:               pair = CP_TREE_NORMAL;  sym = '?'; break;
        }

        wattron(w, COLOR_PAIR(pair));
        char line[128];
        if (ev->type == EVENT_REPARENTED) {
            snprintf(line, sizeof(line), "%c %02d:%02d:%02d %-12s %d→%d",
                     sym, ev->hour, ev->min, ev->sec,
                     ev->name, ev->prev_ppid, ev->new_ppid);
        } else {
            snprintf(line, sizeof(line), "%c %02d:%02d:%02d [%d] %s",
                     sym, ev->hour, ev->min, ev->sec,
                     ev->pid, ev->name);
        }

        char display[128];
        trunc_str(display, line, cols);
        mvwaddstr(w, row, 2, display);
        wattroff(w, COLOR_PAIR(pair));
    }

    wrefresh(w);
}

/* ------------------------------------------------------------------ */
/* Layout: create/resize sub-windows                                   */
/* ------------------------------------------------------------------ */

static void create_windows(UIState *ui)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    /* Header (title) */
    if (ui->win_status)  { delwin(ui->win_status);  ui->win_status  = NULL; }
    if (ui->win_search)  { delwin(ui->win_search);  ui->win_search  = NULL; }
    if (ui->win_tree)    { delwin(ui->win_tree);    ui->win_tree    = NULL; }
    if (ui->win_detail)  { delwin(ui->win_detail);  ui->win_detail  = NULL; }
    if (ui->win_events)  { delwin(ui->win_events);  ui->win_events  = NULL; }

    /* Row allocation:
         row 0       : app title (static)
         row 1       : status bar
         row 2       : search bar
         rows 3..end : tree + detail + events
    */
    int right_cols = cols / 3;
    if (right_cols < 30) right_cols = 30;
    int left_cols  = cols - right_cols;

    int body_rows = rows - 3;
    if (body_rows < 5) body_rows = 5;

    int detail_rows = body_rows / 2;
    if (detail_rows < 8) detail_rows = 8;
    int event_rows = body_rows - detail_rows;
    if (event_rows < 4) event_rows = 4;

    ui->win_status = newwin(1, cols, 1, 0);
    ui->win_search = newwin(1, cols, 2, 0);
    ui->win_tree   = newwin(body_rows, left_cols, 3, 0);
    ui->win_detail = newwin(detail_rows, right_cols, 3, left_cols);
    ui->win_events = newwin(event_rows, right_cols, 3 + detail_rows, left_cols);

    ui->tree_rows  = body_rows;
    ui->tree_cols  = left_cols;
    ui->detail_cols = right_cols;

    scrollok(ui->win_tree, FALSE);
}

/* ------------------------------------------------------------------ */
/* Draw title bar                                                      */
/* ------------------------------------------------------------------ */

static void draw_title(void)
{
    int cols = getmaxx(stdscr);
    attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvhline(0, 0, ' ', cols);
    const char *title = "  ⬢ PROCESS TREE VIEWER & ANALYZER  [PTVA]";
    mvaddstr(0, 0, title);
    /* Right-align version */
    const char *ver = "v1.0 ";
    mvaddstr(0, cols - (int)strlen(ver), ver);
    attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);
    refresh();
}

/* ------------------------------------------------------------------ */
/* Main event loop                                                     */
/* ------------------------------------------------------------------ */

void ui_run(Monitor *m)
{
    /* --- ncurses init --- */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(500);  /* half-second getch() timeout for live refresh */

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_HEADER,       COLOR_WHITE,   COLOR_BLUE);
        init_pair(CP_STATUS,       COLOR_BLACK,   COLOR_CYAN);
        init_pair(CP_TREE_NORMAL,  -1,            -1);
        init_pair(CP_TREE_SELECT,  COLOR_BLACK,   COLOR_YELLOW);
        init_pair(CP_TREE_ZOMBIE,  COLOR_WHITE,   COLOR_RED);
        init_pair(CP_TREE_RUNNING, COLOR_GREEN,   -1);
        init_pair(CP_TREE_SLEEP,   COLOR_CYAN,    -1);
        init_pair(CP_TREE_STOP,    COLOR_MAGENTA, -1);
        init_pair(CP_TREE_SEARCH,  COLOR_BLACK,   COLOR_GREEN);
        init_pair(CP_DETAIL_KEY,   COLOR_CYAN,    -1);
        init_pair(CP_BORDER,       COLOR_BLUE,    -1);
        init_pair(CP_EVENT_CREATE, COLOR_GREEN,   -1);
        init_pair(CP_EVENT_TERM,   COLOR_RED,     -1);
        init_pair(CP_EVENT_ZOMBIE, COLOR_WHITE,   COLOR_RED);
        init_pair(CP_EVENT_REPRT,  COLOR_YELLOW,  -1);
        init_pair(CP_REPARENTED,   COLOR_YELLOW,  -1);
    }

    UIState ui;
    memset(&ui, 0, sizeof(ui));
    ui.mon     = m;
    ui.sel_idx = 0;

    create_windows(&ui);

    int running = 1;

    while (running) {
        /* --- Get current tree --- */
        ProcessTree *t = monitor_lock_tree(m);

        rebuild_visible(&ui, t);
        if (ui.search_buf[0]) tree_search(t, ui.search_buf);

        /* --- Draw everything --- */
        draw_title();
        draw_status(&ui, t, m);
        draw_search(&ui);
        draw_tree(&ui);
        draw_detail(&ui);
        draw_events(&ui);

        monitor_unlock(m);

        /* --- Handle input --- */
        int ch = getch();
        if (ch == ERR) continue;  /* timeout — just redraw */

        if (ui.search_mode) {
            /* Search input mode */
            if (ch == 27 /* ESC */ || ch == '\n') {
                ui.search_mode = 0;
                if (ch == 27) {
                    ui.search_buf[0] = '\0';
                    /* Clear all highlights */
                    ProcessTree *t2 = monitor_lock_tree(m);
                    if (t2) tree_search(t2, NULL);
                    monitor_unlock(m);
                }
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                int len = (int)strlen(ui.search_buf);
                if (len > 0) ui.search_buf[len - 1] = '\0';
            } else if (isprint(ch) && strlen(ui.search_buf) < 254) {
                int len = (int)strlen(ui.search_buf);
                ui.search_buf[len]     = (char)ch;
                ui.search_buf[len + 1] = '\0';
            }
            continue;
        }

        /* Normal mode */
        switch (ch) {
            case 'q':
            case 'Q':
                running = 0;
                break;

            case KEY_UP:
            case 'k':
                if (ui.sel_idx > 0) { ui.sel_idx--; ui.ancestry_mode = 0; }
                break;

            case KEY_DOWN:
            case 'j':
                if (ui.sel_idx < ui.vcount - 1) { ui.sel_idx++; ui.ancestry_mode = 0; }
                break;

            case KEY_PPAGE:
                ui.sel_idx -= ui.tree_rows / 2;
                if (ui.sel_idx < 0) ui.sel_idx = 0;
                break;

            case KEY_NPAGE:
                ui.sel_idx += ui.tree_rows / 2;
                if (ui.sel_idx >= ui.vcount) ui.sel_idx = ui.vcount - 1;
                break;

            case KEY_HOME:
                ui.sel_idx = 0;
                break;

            case KEY_END:
                ui.sel_idx = ui.vcount > 0 ? ui.vcount - 1 : 0;
                break;

            case 'e':
            case '\n':
            case KEY_ENTER:
                /* Toggle collapse */
                if (ui.sel_idx >= 0 && ui.sel_idx < ui.vcount) {
                    Process *p = ui.vnodes[ui.sel_idx].proc;
                    if (p && p->child_count > 0) {
                        p->collapsed = !p->collapsed;
                    }
                }
                break;

            case '/':
                ui.search_mode = 1;
                ui.ancestry_mode = 0;
                break;

            case 27: /* ESC */
                ui.search_buf[0] = '\0';
                ui.search_mode   = 0;
                ui.ancestry_mode = 0;
                break;

            case 'a':
            case 'A':
                ui.ancestry_mode = !ui.ancestry_mode;
                break;

            case 's':
            case 'S': {
                char snap_name[32];
                time_t tnow = time(NULL);
                struct tm *tminfo = localtime(&tnow);
                snprintf(snap_name, sizeof(snap_name), "snap_%02d%02d%02d",
                         tminfo->tm_hour, tminfo->tm_min, tminfo->tm_sec);
                int idx = monitor_save_snapshot(m, snap_name);
                /* Quick notification */
                if (idx >= 0) {
                    /* Flash message in search bar */
                    WINDOW *sw = ui.win_search;
                    werase(sw);
                    wattron(sw, COLOR_PAIR(CP_EVENT_CREATE) | A_BOLD);
                    mvwprintw(sw, 0, 1, "Snapshot saved: %s", snap_name);
                    wattroff(sw, COLOR_PAIR(CP_EVENT_CREATE) | A_BOLD);
                    wrefresh(sw);
                    napms(800);
                }
                break;
            }

            case 'c':
            case 'C':
                if (m->snapshot_count >= 2) {
                    monitor_compare_snapshots(m,
                        m->snapshot_count - 2,
                        m->snapshot_count - 1);
                }
                break;

            case '+':
            case '=':
                m->interval_ms = m->interval_ms > 200 ?
                    m->interval_ms - 200 : 200;
                break;

            case '-':
                m->interval_ms += 200;
                if (m->interval_ms > 10000) m->interval_ms = 10000;
                break;

            case KEY_RESIZE:
                create_windows(&ui);
                clear();
                break;

            default:
                break;
        }
    }

    /* --- Cleanup --- */
    if (ui.win_tree)   delwin(ui.win_tree);
    if (ui.win_detail) delwin(ui.win_detail);
    if (ui.win_events) delwin(ui.win_events);
    if (ui.win_status) delwin(ui.win_status);
    if (ui.win_search) delwin(ui.win_search);

    endwin();
}
