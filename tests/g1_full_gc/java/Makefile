
###################################################
#
# file: Makefile
#
# @Author:   Iacovos G. Kolokasis
# @Version:  20-09-2020
# @email:    kolokasis@ics.forth.gr
#
# Makefile for test files
#
####################################################

ARCH := $(shell lscpu | awk -F: '/Architecture/ {gsub(/^[ \t]+/, "", $$2); print $$2; exit}')
JAVA_REPO := $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../../../jdk17
JAVA_COMP=$(JAVA_REPO)/build/linux-$(ARCH)-server-release/jdk/bin/javac -Xlint:deprecation -classpath ./bin/
JAVAC=$(JAVA_COMP) -d ./bin
LOGDIR := build_logs

TARGET = $(addprefix bin/, \
				 Array.class Array_List.class Array_List_Int.class Array_List_String.class List_Large.class \
				 MultiList.class Simple_Lambda.class Extend_Lambda.class \
				 Test_Reflection.class Test_Reference.class HashMap.class Rehashing.class \
				 Clone.class Groupping.class MultiHashMap.class Test_WeakHashMap.class \
				 ClassInstance.class Humongous.class HumongousChain.class TriggerImplicitGCs.class \
				 Test_TeraHeap_API.class)

.PHONY: header
	   
all: header $(TARGET)

header:
	@printf "\n  \033[1;37m%*s\033[0m\n" 32 "Full GC Test Compilation"
	@printf "  \033[1;37m%-32s\033[0m  \033[1;37m%s\033[0m\n" "Test" "Status"
	@printf "  \033[1;37m%-32s\033[0m  \033[1;37m%s\033[0m\n" "--------------------------------" "--------"

# Ensure log directory exists
$(LOGDIR):
	@mkdir -p $(LOGDIR)

bin/%.class: %.java | $(LOGDIR)
	@printf "  \033[1;37m%-32s\033[0m  " "$*"
	@$(JAVAC) $< >"$(LOGDIR)/$(@F).log" 2>&1 && \
		printf "\033[1;32mSUCCESS\033[0m\n" || { \
		printf "\033[1;31mFAIL\033[0m (log: %s)\n" "$(LOGDIR)/$(@F).log"; \
		exit 1; \
		}

clean:
	@rm -rf out
	@rm -f hotspot_pid*

distclean: clean
	@rm -rf bin build_logs
