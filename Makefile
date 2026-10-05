# Makefile
# Usage:
# 	- make all: compiles src/*.c and generates notvim binary at root
# 	- make test: compiels and runs test suite; returns 0 if success and != 0 if fails
# 	- make clean: removes binary and object files
# 	- Builds use AddressSanitizer + UBSan (memory errors and leaks fail the run).
# 	  Disable with `make clean && make SAN=` (needed for Valgrind, which can't run with ASan).
CC 			= gcc
SAN 		?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS 		= -Wall -Wextra -Werror -pedantic -std=c11 -Iinclude -Itests/unity $(SAN)

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

test: notvim test_runner
	./test_runner

test_runner: $(OBJS_LIB) $(TEST_OBJS) $(UNITY_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lutil

clean:
	rm -f notvim test_runner
	rm -f $(OBJS) $(TEST_OBJS) $(UNITY_OBJS)

.PHONY: all test clean
