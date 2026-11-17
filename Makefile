# The command-line packager, eup, and the host tests. The installer program it carries is the prebuilt
# one in prebuilt/ (build-mac.sh makes it, with Retro68).
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99
CORE = core/ini.c core/paths.c core/format.c core/deflate.c core/inflate.c core/package.c
SHELL_BIN = prebuilt/Installer Shell.bin

all: eup

build/shell.c: $(subst $() ,\ ,$(SHELL_BIN)) tools/bin2c.c
	mkdir -p build
	$(CC) -o build/bin2c tools/bin2c.c
	build/bin2c "$(SHELL_BIN)" eup_shell > $@

eup: cli/eup.c $(CORE) build/shell.c core/*.h
	$(CC) $(CFLAGS) -o $@ cli/eup.c $(CORE) build/shell.c

build/test_core: tests/test_core.c $(CORE) build/shell.c core/*.h
	$(CC) $(CFLAGS) -o $@ tests/test_core.c $(CORE) build/shell.c

test: eup build/test_core
	build/test_core
	tests/test_cli.sh

clean:
	rm -rf build eup

.PHONY: all test clean
