CC = cc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long -fPIC \
    -Iinclude -Isrc -D_POSIX_C_SOURCE=200112L
LIBS = -lsqlite3
SOFLAGS = -shared -Wl,--version-script=src/libfakefs.map

SOURCES = src/db.c src/path.c src/inode.c src/dirent.c src/resolve.c \
    src/perm.c src/fdtable.c src/util.c src/ops.c src/extent.c src/data.c
OBJS = $(SOURCES:src/%.c=build/%.o)

all: build/libfakefs.a build/libfakefs.so

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/libfakefs.a: $(OBJS)
	ar rcs $@ $^

build/libfakefs.so: $(OBJS)
	$(CC) $(SOFLAGS) $^ $(LIBS) -o $@

clean:
	rm -rf build

test: all
	sh test/run_unit.sh
	sh test/run_lib.sh component
	sh test/run_lib.sh integration
	sh test/run_e2e.sh
	sh test/run_property.sh

check: all
	shellcheck -s sh test/*.sh
	shellcheck -s bash test/*.sh
	sh test/verify_symbols.sh
	sh test/check_complexity_test.sh
	sh test/check_complexity.sh

check-leak: all
	sh test/run_unit.sh
	sh test/run_leak.sh

.PHONY: all clean test check check-leak
