# Makefile
# Usage:
# 	- make all: compiles src/*.c and generates notvim binary at root
# 	- make test: compiels and runs test suite; returns 0 if success and != 0 if fails
# 	- make clean: removes binary and object files
# 	- Builds use AddressSanitizer + UBSan (memory errors and leaks fail the run).
# 	  Disable with `make clean && make SAN=`; run `make clean` after changing SAN.
# 	- Objects are rebuilt when an included header changes (-MMD).
# 	- test_runner is linked with --wrap=malloc,realloc,strdup (tests/allocfail.c) so tests can make an allocation fail.
# 	- make check-eol: fails if a tracked text file contains a carriage return (the repo is LF only; make test runs it).
CC 			= gcc
SAN 		?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS 		= -Wall -Wextra -Werror -pedantic -std=c11 -Iinclude -Itests/unity -MMD -MP $(SAN)

SRCS 		= $(wildcard src/*.c)
OBJS 		= $(SRCS:.c=.o)

SRCS_LIB	= $(filter-out src/main.c, $(SRCS))
OBJS_LIB 	= $(SRCS_LIB:.c=.o)

TEST_SRCS	= $(wildcard tests/*.c)
TEST_OBJS 	= $(TEST_SRCS:.c=.o)

UNITY_SRCS	= $(wildcard tests/unity/*.c)
UNITY_OBJS 	= $(UNITY_SRCS:.c=.o)

all: notvim

notvim: $(OBJS)
	$(CC) $(CFLAGS) -o  $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Every tracked text file must be LF only (EOL_FILES can be overridden, for example to test the check)
EOL_FILES ?= $(shell git ls-files)

check-eol:
	@bad=$$(grep -lI "$$(printf '\r')" $(EOL_FILES) 2>/dev/null); \
	if [ -n "$$bad" ]; then echo "carriage return (CRLF) found in:"; echo "$$bad"; exit 1; fi

test: check-eol notvim test_runner
	./test_runner

test_runner: $(OBJS_LIB) $(TEST_OBJS) $(UNITY_OBJS)
	$(CC) $(CFLAGS) -Wl,--wrap=malloc,--wrap=realloc,--wrap=strdup -o $@ $^ -lutil

clean:
	rm -f notvim test_runner
	rm -f $(OBJS) $(TEST_OBJS) $(UNITY_OBJS)
	rm -f $(OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(UNITY_OBJS:.o=.d)

# Rebuild objects when a header they include changes (.d files written by -MMD)
-include $(OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(UNITY_OBJS:.o=.d)

.PHONY: all test clean check-eol
