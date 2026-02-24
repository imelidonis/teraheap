
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
INCLUDEDIR = $(PREFIX)/include

## Depended files
LIBHEADERS =  $(INCLUDEDIR)/regions.h $(INCLUDEDIR)/asyncIO.h $(INCLUDEDIR)/segments.h $(INCLUDEDIR)/sllist.h
LIBREGIONSOBJS = $(SRCDIR)/regions.o $(SRCDIR)/asyncIO.o $(SRCDIR)/segments.o $(SRCDIR)/sllist.o
REGIONSLIBRARY = $(LIBDIR)/libregions.so

TH_ALLOCATE_OBJ = $(TESTDIR)/th_allocate.o
TH_GROUP_OBJ = $(TESTDIR)/th_group.o
TH_FREE_OBJ = $(TESTDIR)/th_free.o
TH_SYNC_OBJ = $(TESTDIR)/th_sync_write.o
TH_ASYNC_OBJ = $(TESTDIR)/th_async_write.o
TH_ALLOCATE_MULTI_REGION = $(TESTDIR)/th_allocate_multi_regions.o

TH_ALLOCATE_EXE = th_allocate.bin
TH_GROUP_EXE = th_group.bin
TH_FREE_EXE = th_free.bin
TH_SYNC_EXE = th_sync_write.bin
TH_ASYNC_EXE = th_async_write.bin
TH_ALLOCATE_MULTI_REGION_EXE = th_allocate_multi_regions.bin

CC = gcc

## Flags
BINFLAG = -c
DEBUGFLAG = -g
OFLAG = -o
WALLFLAG = -Wall -Werror -pedantic
OPTIMZEFLAG = -O3
AIOFLAG = -lrt -pthread

LDFLAGS = $(AIOFLAG)
CFLAGS = $(BINFLAG) $(WALLFLAG) $(OPTIMIZEFLAG)

## Commands
RM = rm -fr
AR = ar -r
CP = cp
MKDIR = mkdir -p
