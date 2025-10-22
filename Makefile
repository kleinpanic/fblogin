# Makefile for fblogin (clean, non-root build; root needed only for install)

# ---- config ----
TARGET   := fblogin
SRC_DIR  := src
OBJ_DIR  := obj
INC_DIR  := include
PREFIX   := /usr/local
BINDIR   := $(PREFIX)/bin

# Banner (issue) file used by pam_issue.so
ISSUE_DIR  ?= system/issue
ISSUE_FILE ?= $(ISSUE_DIR)/issue.fblogin
ISSUE_DEST ?= /etc/issue.fblogin

# Manpage install defaults
MANPREFIX ?= $(PREFIX)/share/man
MAN1DIR   ?= $(MANPREFIX)/man1
MAN1PAGES ?= man/fblogin.1
MAN1NAME  := $(notdir $(MAN1PAGES))

# Compiler/Linker
CC       := gcc
CPPFLAGS := -I$(INC_DIR) -D_GNU_SOURCE -MMD -MP
CFLAGS   := -Wall -Wextra -Wpedantic -O2
LDFLAGS  := -Wl,--as-needed
LDLIBS   := -lpam -lpam_misc

# Optional libsystemd (sd-login diagnostics). Auto-detect.
SYSTEMD_CFLAGS := $(shell pkg-config --cflags libsystemd 2>/dev/null)
SYSTEMD_LIBS   := $(shell pkg-config --libs   libsystemd 2>/dev/null)
ifneq ($(strip $(SYSTEMD_LIBS)),)
CPPFLAGS += $(SYSTEMD_CFLAGS) -DHAVE_SYSTEMD_LOGIN=1
LDLIBS   += $(SYSTEMD_LIBS)
endif

# Sources/Objects
SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)

# ---- rules ----
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $@

# ---- convenience modes ----
debug: CFLAGS := -Wall -Wextra -Wpedantic -O0 -g3
debug: LDFLAGS := -Wl,--as-needed
debug: clean all

release: CFLAGS := -Wall -Wextra -Wpedantic -O3 -DNDEBUG
release: LDFLAGS := -Wl,--as-needed -s
release: clean all

asan: CFLAGS := -Wall -Wextra -Wpedantic -O1 -g3 -fsanitize=address,undefined
asan: LDFLAGS := -fsanitize=address,undefined
asan: clean all

# ---- install ----
install: $(TARGET)
	install -d -m 0755 $(DESTDIR)$(BINDIR)
	install -o root -g root -m 4755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

install-systemd:
	install -d -m 0755 $(DESTDIR)/etc/systemd/system
	install -m 0644 system/systemd/fblogin@.service $(DESTDIR)/etc/systemd/system/fblogin@.service

install-pam:
	install -d -m 0755 $(DESTDIR)/etc/pam.d
	install -m 0644 system/pamd/fblogin $(DESTDIR)/etc/pam.d/fblogin

# Install custom banner used by pam_issue.so (skip gracefully if file missing)
install-issue:
	@if [ -f "$(ISSUE_FILE)" ]; then \
	  echo "Installing banner: $(ISSUE_FILE) -> $(DESTDIR)$(ISSUE_DEST)"; \
	  install -d -m 0755 $(DESTDIR)/etc; \
	  install -m 0644 "$(ISSUE_FILE)" "$(DESTDIR)$(ISSUE_DEST)"; \
	else \
	  echo "No banner file at $(ISSUE_FILE); skipping install-issue"; \
	fi

install-man:
	install -d -m 0755 $(DESTDIR)$(MAN1DIR)
	install -m 0644 $(MAN1PAGES) $(DESTDIR)$(MAN1DIR)/
	@# gzip if available, keep original if not
	@if command -v gzip >/dev/null 2>&1; then \
	  gzip -9nf $(DESTDIR)$(MAN1DIR)/$(MAN1NAME); \
	fi

# Convenience: install everything we ship
install-all: install install-systemd install-pam install-issue install-man

# ---- uninstall ----
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall-systemd:
	rm -f $(DESTDIR)/etc/systemd/system/fblogin@.service
	rm -f $(DESTDIR)/etc/systemd/system/multi-user.target.wants/fblogin@*.service

uninstall-pam:
	rm -f $(DESTDIR)/etc/pam.d/fblogin

uninstall-issue:
	rm -f $(DESTDIR)$(ISSUE_DEST)

uninstall-man:
	rm -f $(DESTDIR)$(MAN1DIR)/$(MAN1NAME) $(DESTDIR)$(MAN1DIR)/$(MAN1NAME).gz

uninstall-all: uninstall uninstall-systemd uninstall-pam uninstall-issue uninstall-man

# ---- clean ----
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

distclean: clean

.PHONY: all debug release asan install uninstall clean distclean \
        install-systemd install-pam install-issue install-man install-all \
        uninstall-systemd uninstall-pam uninstall-issue uninstall-man uninstall-all
-include $(DEPS)

