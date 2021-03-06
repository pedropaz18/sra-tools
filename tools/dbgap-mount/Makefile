# ===========================================================================
#
#                            PUBLIC DOMAIN NOTICE
#               National Center for Biotechnology Information
#
#  This software/database is a "United States Government Work" under the
#  terms of the United States Copyright Act.  It was written as part of
#  the author's official duties as a United States Government employee and
#  thus cannot be copyrighted.  This software/database is freely available
#  to the public for use. The National Library of Medicine and the U.S.
#  Government have not placed any restriction on its use or reproduction.
#
#  Although all reasonable efforts have been taken to ensure the accuracy
#  and reliability of the software and data, the NLM and the U.S.
#  Government do not and cannot warrant the performance or results that
#  may be obtained by using this software or data. The NLM and the U.S.
#  Government disclaim all warranties, express or implied, including
#  warranties of performance, merchantability or fitness for any particular
#  purpose.
#
#  Please cite the author in any work or product based on this material.
#
# ===========================================================================


default: std

TOP ?= $(abspath ../..)
MODULE = tools/dbgap-mount

include $(TOP)/build/Makefile.env


ifeq (win,$(OS))
    DOCAN_LIBS = -lDOKAN
else
    DOCAN_LIBS = -lfuse
endif

ifeq (mac,$(OS))
TOP = $(shell cd ../.. && pwd )
DOCAN_LIBS = -L/usr/local/lib -losxfuse
endif

INT_TOOLS = \

EXT_TOOLS = \
	demo    \
	dbgap-mount-tool
    
ALL_TOOLS = \
	$(INT_TOOLS) \
	$(EXT_TOOLS)

#-------------------------------------------------------------------------------
# outer targets
#
all std: vers-includes
	@ $(MAKE_CMD) $(TARGDIR)/$@

$(ALL_TOOLS): vers-includes
	@ $(MAKE_CMD) $(BINDIR)/$@

.PHONY: all std $(ALL_TOOLS)

#-------------------------------------------------------------------------------
# all
#
$(TARGDIR)/all: \
        $(addprefix $(BINDIR)/,$(ALL_TOOLS))

.PHONY: $(TARGDIR)/all

#-------------------------------------------------------------------------------
# std
#
$(TARGDIR)/std: \
	$(addprefix $(BINDIR)/,$(EXT_TOOLS))

.PHONY: $(TARGDIR)/std

#-------------------------------------------------------------------------------
# vers-includes
#
$(TARGDIR)/vers-includes: $(addsuffix .vers.h,$(EXT_TOOLS))

.PHONY: $(TARGDIR)/vers-includes

#-------------------------------------------------------------------------------
# clean
#
clean: stdclean

.PHONY: clean

#-------------------------------------------------------------------------------
# tag
#
tag: \
	$(addsuffix _tag,$(ALL_TOOLS))

.PHONY: tag $(addsuffix _tag,$(ALL_TOOLS))


#-------------------------------------------------------------------------------
# demo
#  test program for new XFS toy
#
ZDB_DEMO_SRC = \
	demo

ZDB_DEMO_OBJ = \
	$(addsuffix .$(OBJX),$(ZDB_DEMO_SRC))

ZDB_DEMO_LIB = -lkapp -lncbi-vdb -lxfs $(DOCAN_LIBS)

$(BINDIR)/demo: $(ZDB_DEMO_OBJ)
	$(LD) --exe --vers $(SRCDIR) -o $@ $^ $(ZDB_DEMO_LIB)

demo_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) demo $(ZDB_DEMO_OBJ)

#-------------------------------------------------------------------------------
# demo
#  test program for new XFS toy
#
ZDB_TOOL_SRC = \
    wrap    \
	dbgap-mount-tool

ZDB_TOOL_OBJ = \
	$(addsuffix .$(OBJX),$(ZDB_TOOL_SRC))

# ZDB_TOOL_LIB = -lkapp -lncbi-vdb -lxfs $(DOCAN_LIBS)
ZDB_TOOL_LIB = -sxfs -skapp -sncbi-vdb -skfg $(DOCAN_LIBS)

$(BINDIR)/dbgap-mount-tool: $(ZDB_TOOL_OBJ)
	$(LD) --exe --vers $(SRCDIR) -o $@ $^ $(ZDB_TOOL_LIB)

dbgap-mount-tool_tag:
	@ $(TOP)/build/tag-module.sh $(MODULE) dbgap-mount-tool $(ZDB_TOOL_OBJ)

