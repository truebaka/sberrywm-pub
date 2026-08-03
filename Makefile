CC = cc
PKGS = wlroots-0.19 wayland-server wayland-protocols xkbcommon
CFLAGS += -g -Wall -Wextra -DWLR_USE_UNSTABLE -I. $(shell pkg-config --cflags $(PKGS))
LIBS = $(shell pkg-config --libs $(PKGS))
WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

XDG_SHELL_XML = $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml
LAYER_SHELL_XML = wlr-layer-shell-unstable-v1.xml
POINTER_CONSTRAINTS_XML = $(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
RELATIVE_POINTER_XML = $(WAYLAND_PROTOCOLS)/unstable/relative-pointer/relative-pointer-unstable-v1.xml
CURSOR_SHAPE_XML = $(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml
TABLET_V2_XML = $(WAYLAND_PROTOCOLS)/stable/tablet/tablet-v2.xml

PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin
SESSIONDIR ?= $(PREFIX)/share/wayland-sessions
DESTDIR    ?=

PROTO_HEADERS = xdg-shell-protocol.h \
	wlr-layer-shell-unstable-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h \
	relative-pointer-unstable-v1-protocol.h \
	cursor-shape-v1-protocol.h \
	tablet-v2-protocol.h

PROTO_SRCS = xdg-shell-protocol.c \
	wlr-layer-shell-unstable-v1-protocol.c \
	pointer-constraints-unstable-v1-protocol.c \
	relative-pointer-unstable-v1-protocol.c \
	cursor-shape-v1-protocol.c \
	tablet-v2-protocol.c

all: strawberrywm

xdg-shell-protocol.h xdg-shell-protocol.c: $(XDG_SHELL_XML)
	$(WAYLAND_SCANNER) server-header $(XDG_SHELL_XML) xdg-shell-protocol.h
	$(WAYLAND_SCANNER) private-code $(XDG_SHELL_XML) xdg-shell-protocol.c

wlr-layer-shell-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.c: $(LAYER_SHELL_XML)
	$(WAYLAND_SCANNER) server-header $(LAYER_SHELL_XML) wlr-layer-shell-unstable-v1-protocol.h
	$(WAYLAND_SCANNER) private-code $(LAYER_SHELL_XML) wlr-layer-shell-unstable-v1-protocol.c

pointer-constraints-unstable-v1-protocol.h pointer-constraints-unstable-v1-protocol.c: $(POINTER_CONSTRAINTS_XML)
	$(WAYLAND_SCANNER) server-header $(POINTER_CONSTRAINTS_XML) pointer-constraints-unstable-v1-protocol.h
	$(WAYLAND_SCANNER) private-code $(POINTER_CONSTRAINTS_XML) pointer-constraints-unstable-v1-protocol.c

relative-pointer-unstable-v1-protocol.h relative-pointer-unstable-v1-protocol.c: $(RELATIVE_POINTER_XML)
	$(WAYLAND_SCANNER) server-header $(RELATIVE_POINTER_XML) relative-pointer-unstable-v1-protocol.h
	$(WAYLAND_SCANNER) private-code $(RELATIVE_POINTER_XML) relative-pointer-unstable-v1-protocol.c

cursor-shape-v1-protocol.h cursor-shape-v1-protocol.c: $(CURSOR_SHAPE_XML)
	$(WAYLAND_SCANNER) server-header $(CURSOR_SHAPE_XML) cursor-shape-v1-protocol.h
	$(WAYLAND_SCANNER) private-code $(CURSOR_SHAPE_XML) cursor-shape-v1-protocol.c

tablet-v2-protocol.h tablet-v2-protocol.c: $(TABLET_V2_XML)
	$(WAYLAND_SCANNER) server-header $(TABLET_V2_XML) tablet-v2-protocol.h
	$(WAYLAND_SCANNER) private-code $(TABLET_V2_XML) tablet-v2-protocol.c

strawberrywm: main.c config.h $(PROTO_SRCS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) -o $@ main.c $(PROTO_SRCS) $(LIBS)

startsberry:
	@echo '#!/bin/bash' > startsberry
	@echo 'export XDG_SESSION_TYPE=wayland' >> startsberry
	@echo 'export XDG_CURRENT_DESKTOP=sberrywm' >> startsberry
	@echo 'export XDG_DESKTOP_PORTAL=1' >> startsberry
	@echo '' >> startsberry
	@echo '# Prefer server-side decorations / reduce toolkit CSD' >> startsberry
	@echo 'export GTK_CSD=0' >> startsberry
	@echo 'export GTK_USE_PORTAL=0' >> startsberry
	@echo 'export QT_WAYLAND_DISABLE_WINDOWDECORATION=1' >> startsberry
	@echo 'export QT_QPA_PLATFORM=wayland' >> startsberry
	@echo 'export ELECTRON_OZONE_PLATFORM_HINT=wayland' >> startsberry
	@echo 'export MOZ_ENABLE_WAYLAND=1' >> startsberry
	@echo 'exec strawberrywm' >> startsberry
	chmod +x startsberry

sberrywm.desktop:
	@echo '[Desktop Entry]' > sberrywm.desktop
	@echo 'Name=SberryWM' >> sberrywm.desktop
	@echo 'Comment=Strawberry Wayland Compositor' >> sberrywm.desktop
	@echo 'Exec=startsberry' >> sberrywm.desktop
	@echo 'TryExec=startsberry' >> sberrywm.desktop
	@echo 'Type=Application' >> sberrywm.desktop
	@echo 'DesktopNames=sberrywm' >> sberrywm.desktop

install: strawberrywm startsberry sberrywm.desktop
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(SESSIONDIR)
	rm -f $(DESTDIR)$(BINDIR)/strawberrywm
	rm -f $(DESTDIR)$(BINDIR)/startsberry
	rm -f $(DESTDIR)$(SESSIONDIR)/sberrywm.desktop
	cp -f strawberrywm $(DESTDIR)$(BINDIR)/strawberrywm
	chmod 755 $(DESTDIR)$(BINDIR)/strawberrywm
	cp -f startsberry $(DESTDIR)$(BINDIR)/startsberry
	chmod 755 $(DESTDIR)$(BINDIR)/startsberry
	cp -f sberrywm.desktop $(DESTDIR)$(SESSIONDIR)/sberrywm.desktop
	@echo ""
	@echo "Installed:"
	@echo "  $(DESTDIR)$(BINDIR)/strawberrywm"
	@echo "  $(DESTDIR)$(BINDIR)/startsberry"
	@echo "  $(DESTDIR)$(SESSIONDIR)/sberrywm.desktop"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/strawberrywm
	rm -f $(DESTDIR)$(BINDIR)/startsberry
	rm -f $(DESTDIR)$(SESSIONDIR)/sberrywm.desktop

clean:
	rm -f strawberrywm startsberry sberrywm.desktop \
	      xdg-shell-protocol.h xdg-shell-protocol.c \
	      wlr-layer-shell-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.c \
	      pointer-constraints-unstable-v1-protocol.h pointer-constraints-unstable-v1-protocol.c \
	      relative-pointer-unstable-v1-protocol.h relative-pointer-unstable-v1-protocol.c \
	      cursor-shape-v1-protocol.h cursor-shape-v1-protocol.c \
	      tablet-v2-protocol.h tablet-v2-protocol.c

.PHONY: all clean install uninstall startsberry sberrywm.desktop
