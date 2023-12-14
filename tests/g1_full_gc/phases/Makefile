
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

JAVA_REPO := $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../../../jdk17
# JAVA_COMP=$(JAVA_REPO)/build/base-linux/jdk/bin/javac -Xlint:deprecation -sourcepath ../Whitebox/wb.jar -classpath ./bin/
JAVA_COMP=$(JAVA_REPO)/build/linux-x86_64-server-slowdebug/jdk/bin/javac -Xlint:deprecation -sourcepath ../Whitebox/wb.jar -classpath ./bin/


JAVAC=$(JAVA_COMP) -d ./bin

GDB=gdb

TARGET=Phase1_MarkOneObject.class Phase1_MarkSubObject.class Phase2_GiveAddressesFromH2.class

all: $(TARGET)

Phase1_MarkOneObject.class: Phase1_MarkOneObject.java
	$(JAVAC) $<

Phase1_MarkSubObject.class: Phase1_MarkSubObject.java
	$(JAVAC) $<

Phase2_GiveAddressesFromH2.class: Phase2_GiveAddressesFromH2.java
	$(JAVAC) $<

clean:
	rm -rf out
	rm -f hotspot_pid*

distclean: clean
	rm -rf bin
