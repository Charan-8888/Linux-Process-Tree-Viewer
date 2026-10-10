# ===========================================================================
# Makefile — Process Tree Viewer & Analyzer (PTVA)
# ===========================================================================
# Requirements:
#   gcc, make, ncurses development headers (libncurses-dev / ncurses-devel)
#
# Install on Debian/Ubuntu:   sudo apt install libncurses-dev
# Install on Fedora/RHEL:     sudo dnf install ncurses-devel
#
# Targets:
#   make          — build ptva (default)
#   make tests    — build test programs
#   make all      — build ptva + test programs
#   make clean    — remove all build artifacts
#   make run      — build and run ptva
# ===========================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -pedantic -g \
           -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700
LDFLAGS = -lncurses -lpthread -lm

SRCDIR  = src
TESTDIR = tests
BINDIR  = .

# Source files (excluding main.c so we can list them separately)
LIB_SRCS = $(SRCDIR)/process.c \
            $(SRCDIR)/proc_reader.c \
            $(SRCDIR)/tree.c \
            $(SRCDIR)/analyzer.c \
            $(SRCDIR)/monitor.c \
            $(SRCDIR)/ui.c

MAIN_SRC = $(SRCDIR)/main.c

LIB_OBJS = $(LIB_SRCS:.c=.o)
MAIN_OBJ = $(MAIN_SRC:.c=.o)

TARGET   = $(BINDIR)/ptva

TEST_SRCS = $(TESTDIR)/test_fork_zombie.c \
             $(TESTDIR)/test_reparent.c

TEST_BINS = $(TESTDIR)/test_fork_zombie \
             $(TESTDIR)/test_reparent

# ---------------------------------------------------------------------------
.PHONY: all clean run tests

all: $(TARGET) tests

$(TARGET): $(LIB_OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build successful: $@"

# Generic .c → .o rule
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------------------------------------------------------------------------
# Test programs (no ncurses needed)
# ---------------------------------------------------------------------------
tests: $(TEST_BINS)

$(TESTDIR)/test_fork_zombie: $(TESTDIR)/test_fork_zombie.c
	$(CC) $(CFLAGS) -o $@ $<
	@echo "Built: $@"

$(TESTDIR)/test_reparent: $(TESTDIR)/test_reparent.c
	$(CC) $(CFLAGS) -o $@ $<
	@echo "Built: $@"

# ---------------------------------------------------------------------------
run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(LIB_OBJS) $(MAIN_OBJ) $(TARGET) $(TEST_BINS)
	@echo "Cleaned."

# ---------------------------------------------------------------------------
# Dependency tracking (auto-generated)
# ---------------------------------------------------------------------------
DEPS = $(LIB_OBJS:.o=.d) $(MAIN_OBJ:.o=.d)
-include $(DEPS)

%.d: %.c
	@$(CC) $(CFLAGS) -MM -MT '$(<:.c=.o)' $< > $@
