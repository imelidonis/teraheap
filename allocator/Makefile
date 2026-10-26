###################################################
#
# file: Makefile
#
# @Author:   Iacovos G. Kolokasis
# @Version:  09-03-2021
# @email:    kolokasis@ics.forth.gr
#
###################################################

include ./Makefile_common.mk
include ./makefile.colors

.PHONY: all lib tests test debug install uninstall clean distclean dependencies

# Library and all test programs
all: lib tests

# Only the shared library (what the JVM links against)
lib: $(LIBRARY)

tests: $(TEST_BINS)

# Keep the test objects (make would delete them as intermediate files)
.SECONDARY: $(TEST_OBJS)

# Clean debug build of the library (-g -O0 -DASSERT)
debug:
	@$(MAKE) --no-print-directory distclean
	@$(MAKE) --no-print-directory DEBUG=1 lib

$(LIBRARY): $(LIBOBJS) | $(LIBDIR)
	@echo -e '[$(BYELLOW)LINK$(RESET)]' $(LIBRARY)
	@$(CC) $(LIBOBJS) -shared -o $(LIBRARY) $(LDFLAGS)

$(BINDIR)/%.bin: $(TESTDIR)/%.o $(LIBRARY) | $(BINDIR)
	@echo -e '[$(BYELLOW)LNK$(RESET)]' $@
	@$(CC) $< $(LIBRARY) -Wl,-rpath,$(LIBDIR) $(LDFLAGS) -o $@

%.o: %.c
	@echo -e '[$(BGREEN)GCC$(RESET)]' $<
	@$(CC) $(CFLAGS) $< -o $@

$(LIBDIR) $(BINDIR):
	@$(MKDIR) $@

# Header dependencies written by -MMD
-include $(LIBOBJS:.o=.d) $(TEST_OBJS:.o=.d)

# Run every test program; each one creates its H2 file in TEST_H2_DIR
test: tests
	@fail=0; \
	for t in $(TEST_BINS); do \
		name=$$(basename $$t .bin); \
		if $$t $(TEST_H2_DIR) > $(BINDIR)/$$name.log 2>&1; then \
			printf '%b %s\n' '[$(BGREEN)PASS$(RESET)]' $$name; \
		else \
			printf '%b %s\n' '[$(BRED)FAIL$(RESET)]' "$$name (see $(BINDIR)/$$name.log)"; fail=1; \
		fi; \
	done; \
	exit $$fail

install: $(LIBRARY)
	@echo -e '[$(BGREEN)INSTALL$(RESET)]' $(LIBRARY)
	@$(CP) $(LIBRARY) $(INSTALL_PREFIX)/lib/
	@echo -e '[$(BYELLOW)LN$(RESET)]' /usr/lib/lib$(LIBNAME).so
	@ln -sf $(INSTALL_PREFIX)/lib/lib$(LIBNAME).so /usr/lib/lib$(LIBNAME).so
	@echo -e '[$(BYELLOW)LN$(RESET)]' /usr/lib64/lib$(LIBNAME).so
	@ln -sf $(INSTALL_PREFIX)/lib/lib$(LIBNAME).so /usr/lib64/lib$(LIBNAME).so
	@echo -e '[$(BGREEN)INSTALL$(RESET)]' $(INCLUDEDIR)/h2_allocator.h
	@$(CP) $(INCLUDEDIR)/h2_allocator.h $(INSTALL_PREFIX)/include/

uninstall:
	@echo -e '[$(BRED)UNINSTALL$(RESET)]' lib$(LIBNAME).so
	@$(RM) /usr/lib/lib$(LIBNAME).so /usr/lib64/lib$(LIBNAME).so $(INSTALL_PREFIX)/lib/lib$(LIBNAME).so
	@echo -e '[$(BRED)UNINSTALL$(RESET)]' $(INSTALL_PREFIX)/include/regions.h
	@$(RM) $(INSTALL_PREFIX)/include/regions.h

clean:
	@echo -e '[$(BRED)RM$(RESET)]' objects and dependency files
	@$(RM) $(LIBOBJS) $(LIBOBJS:.o=.d) $(TEST_OBJS) $(TEST_OBJS:.o=.d)

distclean: clean
	@echo -e '[$(BRED)RM$(RESET)]' $(BINDIR) $(LIBDIR)
	@$(RM) $(BINDIR) $(LIBDIR)

dependencies:
	@test ! -d "C-Thread-Pool" && git clone git@github.com:Pithikos/C-Thread-Pool.git
