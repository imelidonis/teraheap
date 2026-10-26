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

#rules
.PHONY: all lib th_allocate th_group th_free th_sync th_async th_shadow clean distclean test depedencies

all: th_allocate th_group th_free th_sync th_async th_multi_region

lib: $(REGIONSLIBRARY)

th_allocate: $(TH_ALLOCATE_EXE)

th_group: $(TH_GROUP_EXE)

th_free: $(TH_FREE_EXE)

th_sync: $(TH_SYNC_EXE)

th_async: $(TH_ASYNC_EXE)

th_multi_region: $(TH_ALLOCATE_MULTI_REGION_EXE)

th_shadow: $(TH_SHADOW_EXE)

$(TH_SHADOW_EXE): $(TH_SHADOW_OBJ) $(REGIONSLIBRARY)
	@echo -e '[$(BYELLOW)LNK$(RESET)]' $^
	@$(CC) $< $(REGIONSLIBRARY) $(LDFLAGS) $(OFLAG) $@

$(TH_ALLOCATE_MULTI_REGION_EXE): $(TH_ALLOCATE_MULTI_REGION) $(REGIONSLIBRARY)
	@echo -e '[$(BYELLOW)LNK$(RESET)]' $^
	@$(CC) $< $(REGIONSLIBRARY) $(OFLAG) $@ 

$(TH_ALLOCATE_EXE): $(TH_ALLOCATE_OBJ) $(REGIONSLIBRARY)
	@echo -e '[$(BYELLOW)LNK$(RESET)]' $^
	@$(CC) $< $(REGIONSLIBRARY) $(OFLAG) $@ 

$(TH_GROUP_EXE): $(TH_GROUP_OBJ) $(REGIONSLIBRARY)
	@echo -e '[$(BYELLOW)LNK$(RESET)]' $^
	@$(CC) $< $(REGIONSLIBRARY) $(OFLAG) $@ 

$(TH_FREE_EXE): $(TH_FREE_OBJ) $(REGIONSLIBRARY)
	@echo -e '[$(BYELLOW)LNK$(RESET)]' $^
	@$(CC) $< $(REGIONSLIBRARY) $(OFLAG) $@ 

$(TH_SYNC_EXE): $(TH_SYNC_OBJ) $(REGIONSLIBRARY)
	@echo -e '[$(BYELLOW)LNK$(RESET)]' $^
	@$(CC) $< $(REGIONSLIBRARY) $(OFLAG) $@ 

$(TH_ASYNC_EXE): $(TH_ASYNC_OBJ) $(REGIONSLIBRARY)
	@echo -e '[$(BYELLOW)LNK$(RESET)]' $^
	@$(CC) $< $(REGIONSLIBRARY) $(OFLAG) $@ 

$(REGIONSLIBRARY): $(LIBREGIONSOBJS)
	@echo -e '[$(BBLUE)MKDIR$(RESET)]' $(LIBDIR)
	@$(MKDIR) $(LIBDIR)
	@echo -e '[$(BYELLOW)LINK$(RESET)]' $(REGIONSLIBRARY)
	@$(CC) $(LIBREGIONSOBJS) -shared -o $(REGIONSLIBRARY) $(LDFLAGS)

%.o: %.c
	@echo -e '[$(BGREEN)GCC$(RESET)]' $<
	@$(CC) $(CFLAGS) -fPIC $< $(OFLAG) $@

%.o: $(LIBHEADERS)

install:
	@echo -e '[$(BGREEN)INSTALL$(RESET)]' $(REGIONSLIBRARY)
	@$(CP) lib/libregions.so $(INSTALL_PREFIX)/lib/
	@echo -e '[$(BYELLOW)LN$(RESET)]' $(REGIONSLIBRARY)
	@ln -s $(INSTALL_PREFIX)/lib/libregions.so /usr/lib/libregions.so
	@echo -e '[$(BYELLOW)LN$(RESET)]' $(REGIONSLIBRARY)
	@ln -s $(INSTALL_PREFIX)/lib/libregions.so /usr/lib64/libregions.so
	@echo -e '[$(BGREEN)INSTALL$(RESET)]' $(INCLUDEDIR)/regions.h
	@$(CP) include/regions.h $(INSTALL_PREFIX)/include/

uninstall:
	@echo -e '[$(BRED)UNINSTALL$(RESET)]' /usr/lib/libregions.so
	@rm /usr/lib/libregions.so
	@echo -e '[$(BRED)UNINSTALL$(RESET)]' /usr/lib64/libregions.so
	@rm /usr/lib64/libregions.so
	@echo -e '[$(BRED)UNINSTALL$(RESET)]' $(INSTALL_PREFIX)/lib/libregions.so
	@rm $(INSTALL_PREFIX)/lib/libregions.so
	@echo -e '[$(BRED)UNINSTALL$(RESET)]' $(INSTALL_PREFIX)/include/regions.h
	@rm $(INSTALL_PREFIX)/include/regions.h

clean:
	@echo -e '[$(BRED)RM$(RESET)]' $(TH_ALLOCATE_OBJ) $(TH_SYNC_OBJ) $(TH_ASYNC_OBJ) $(TH_GROUP_OBJ) $(TH_FREE_OBJ)
	@$(RM) $(LIBREGIONSOBJS) $(TH_ALLOCATE_OBJ) $(TH_SYNC_OBJ) $(TH_ASYNC_OBJ) $(TH_GROUP_OBJ) $(TH_FREE_OBJ) $(TH_ALLOCATE_MULTI_REGION) $(TH_SHADOW_OBJ)

distclean: clean
	@echo -e '[$(BRED)RM$(RESET)]' $(TH_ALLOCATE_EXE) $(TH_SYNC_EXE) $(TH_ASYNC_EXE) $(TH_GROUP_EXE) $(TH_FREE_EXE)
	@$(RM) $(TH_ALLOCATE_EXE) $(TH_SYNC_EXE) $(TH_ASYNC_EXE) $(TH_GROUP_EXE) $(TH_FREE_EXE) $(TH_ALLOCATE_MULTI_REGION_EXE) $(TH_SHADOW_EXE)
	@echo -e '[$(BRED)RM$(RESET)]' $(REGIONSLIBRARY)
	@$(RM) $(REGIONSLIBRARY)
	@echo -e '[$(BRED)RM$(RESET)]' $(LIBDIR)
	@$(RM) $(LIBDIR)

test:
	@for t in $$(ls *.bin); do \
		./$$t 2> out; \
	done 
	@rm out

dependencies:
	@test ! -d "C-Thread-Pool" && git clone git@github.com:Pithikos/C-Thread-Pool.git
