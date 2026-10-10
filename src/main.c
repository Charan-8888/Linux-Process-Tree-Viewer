/**
 * main.c — Entry point for Process Tree Viewer & Analyzer.
 *
 * Parses command-line options, starts the monitor (background refresh
 * thread), and hands control to the ncurses UI.
 *
 * Usage:
 *   ptva [-i interval_ms]
 *
 *   -i <ms>   Refresh interval in milliseconds (default: 1000)
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>

#include "monitor.h"
#include "ui.h"

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-i interval_ms]\n"
            "\n"
            "  -i <ms>    Refresh interval in milliseconds (default: 1000)\n"
            "  -h         Show this help\n"
            "\n"
            "Keyboard controls (once running):\n"
            "  ↑ ↓        Navigate the process tree\n"
            "  Enter / e  Expand or collapse a subtree\n"
            "  /          Enter search mode\n"
            "  Esc        Clear search\n"
            "  a          Toggle ancestry view for selected process\n"
            "  s          Save a named snapshot\n"
            "  c          Compare last two snapshots\n"
            "  + / -      Increase / decrease refresh interval\n"
            "  q          Quit\n",
            prog);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int interval_ms = REFRESH_INTERVAL_MS;

    int opt;
    while ((opt = getopt(argc, argv, "i:h")) != -1) {
        switch (opt) {
            case 'i':
                interval_ms = atoi(optarg);
                if (interval_ms < 100) {
                    fprintf(stderr, "Warning: interval < 100 ms; clamping to 100 ms\n");
                    interval_ms = 100;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Check we are on Linux */
    if (access("/proc", F_OK) != 0) {
        fprintf(stderr,
                "Error: /proc is not accessible. "
                "This tool requires Linux.\n");
        return 1;
    }

    /* Start the monitor */
    Monitor *mon = monitor_start(interval_ms);
    if (!mon) {
        fprintf(stderr, "Error: failed to start process monitor.\n");
        return 1;
    }

    /* Run the TUI (blocks until user quits) */
    ui_run(mon);

    /* Clean up */
    monitor_stop(mon);

    return 0;
}
