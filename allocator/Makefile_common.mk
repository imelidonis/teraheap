
###################################################
#
# file: Makefile_common.mk
#
# @Author:   Iacovos G. Kolokasis
# @Version:  09-03-2021
# @email:    kolokasis@ics.forth.gr
#
###################################################

## Library path
PREFIX := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
## Install path
INSTALL_PREFIX := /usr/local

## Library directories
SRCDIR = $(PREFIX)/src
TESTDIR = $(PREFIX)/tests
LIBDIR = $(PREFIX)/lib
BINDIR = $(PREFIX)/bin
INCLUDEDIR = $(PREFIX)/include

## Library
LIBNAME = h2allocator
LIBRARY = $(LIBDIR)/lib$(LIBNAME).so
LIBOBJS = $(SRCDIR)/mem_pool.o $(SRCDIR)/sync_io.o $(SRCDIR)/async_io.o $(SRCDIR)/regions.o $(SRCDIR)/region_deps.o $(SRCDIR)/reclaim.o $(SRCDIR)/promo_buffer.o $(SRCDIR)/stats.o $(SRCDIR)/shadow.o

## Tests: every tests/th_*.c becomes bin/th_*.bin
TEST_SRCS = $(wildcard $(TESTDIR)/th_*.c)
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_BINS = $(patsubst $(TESTDIR)/%.c,$(BINDIR)/%.bin,$(TEST_SRCS))
## White-box tests call internal (hidden) functions: they link the objects
## instead of the library
WHITEBOX_BINS = $(BINDIR)/th_free.bin $(BINDIR)/th_shadow.bin

## Directory where the tests create the H2 file (passed as the first argument)
TEST_H2_DIR ?= /mnt/fmap/

CC = gcc

## Flags
WALLFLAG = -Wall -Werror -pedantic
OPTIMIZEFLAG = -O3
# Write a .d file next to each object so that a header change rebuilds it
DEPFLAGS = -MMD -MP
AIOFLAG = -lrt -pthread

# DEBUG=1 (build.sh -d, make debug): debug symbols, no optimization, asserts on
ifeq ($(DEBUG),1)
OPTIMIZEFLAG = -g -O0 -DASSERT
endif

LDFLAGS = $(AIOFLAG)
# Hidden by default: the library exports only the H2_API functions of
# h2_allocator.h
CFLAGS = -c -fPIC -fvisibility=hidden -I$(INCLUDEDIR) $(DEPFLAGS) $(WALLFLAG) $(OPTIMIZEFLAG)

## Commands
RM = rm -fr
CP = cp
MKDIR = mkdir -p
