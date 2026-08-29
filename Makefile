#-------------------------------------------------------------------------
#
#  Copyright (c) 2021 Rajit Manohar
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor,
#  Boston, MA  02110-1301, USA.
#
#-------------------------------------------------------------------------
EXE=interact.$(EXT)

TARGETS=$(EXE)
SUBDIRS=scripts

OBJS=main.o act_cmds.o conf_cmds.o misc_cmds.o act_flprint.o \
	act_simfile.o ptr_manager.o ckt_cmds.o flow.o \
	timer_cmds.o pandr_cmds.o placement_cmds.o \
	routing_cmds.o synth_cmds.o layout_lifecycle.o timing_driven_placement_p2b.o \
	topology_checkpoint_host.o dali_qt_gui_bridge.o

P2B_TEST_EXE=interact-p2b-test.$(EXT)
P2B_TEST_OBJS=$(filter-out placement_cmds.o timer_cmds.o timing_driven_placement_p2b.o,$(OBJS)) \
	timer_cmds.p2btest.o placement_cmds.p2btest.o timing_driven_placement_p2b.p2btest.o
UNIT_TEST_EXE=timing-driven-helpers-test.$(EXT)
UNIT_TEST_OBJS=timing_driven_helpers_test.o

CPPSTD=c++17
SRCS=$(OBJS:.o=.cc)

EXTRALIBDEPEND=

include $(ACT_HOME)/scripts/Makefile.std
include config.mk

OMPFLAG=
GALOIS_EDA_PIECES=

ifdef timing_actpin_INCLUDE 

GALOIS_EDA_PIECES+=-lactcyclone -lcyclone -lacttpass -lgalois_eda
EXTRALIBDEPEND+=$(ACT_HOME)/lib/libactcyclone.a \
	$(ACT_HOME)/lib/libacttpass.so \
	$(ACT_HOME)/lib/libcyclone.a \
	$(ACT_HOME)/lib/libgalois_eda.a

endif

ifdef galois_INCLUDE
GALOIS_EDA_PIECES+=-lgalois_shmem
EXTRALIBDEPEND+=$(ACT_HOME)/lib/libgalois_shmem.a 
OMPFLAG=-fopenmp

ifeq ($(BASEOS),linux)
GALOIS_EDA_PIECES+=-lnuma -latomic
endif

endif

ifdef dali_INCLUDE
DALI_PIECES=-ldalilib -lboost_filesystem -lboost_log_setup -lboost_log -lboost_thread
EXTRALIBDEPEND+=$(ACT_HOME)/lib/libdalilib.a

# Dali decides whether it has a viewer: it installs libdaligui.a only when it
# was built against Qt. Following that installed library, discovered by
# ./configure like every other optional package, keeps one source of truth --
# probing Qt again here would let an interact built on a machine that has Qt
# believe in a viewer that the installed Dali does not carry.
#
# Qt's own flags are still needed to link daligui, so a daligui installed
# against a Qt that pkg-config can no longer see is an inconsistent
# installation and is reported rather than silently dropped.
ifdef daligui_LIBDIR
ifneq ($(shell pkg-config --exists Qt6Widgets 2>/dev/null && echo yes),)
DALI_GUI_PIECES=-ldaligui
DALI_GUI_QT_LIBS=$(shell pkg-config --libs Qt6Widgets)
CFLAGS+=-DINTERACT_HAS_DALI_QT_GUI $(shell pkg-config --cflags Qt6Widgets)
EXTRALIBDEPEND+=$(ACT_HOME)/lib/libdaligui.a
else
$(error libdaligui.a is installed but pkg-config cannot find Qt6Widgets; the Dali installation and the Qt toolchain disagree)
endif
endif

ifeq ($(BASEOS),darwin)
ifeq ($(shell ./have_boost_mt),1)
DALI_PIECES+=-lboost_filesystem-mt -lboost_log_setup-mt -lboost_log-mt -lboost_thread-mt
endif
endif

ifdef NEED_LIBCXXFS
DALI_PIECES+=-lstdc++fs
endif

endif

PANDR_PIECES=$(DALI_GUI_PIECES) $(DALI_PIECES)

ifdef phydb_INCLUDE
PANDR_PIECES+=-lphydb -llef -ldef
EXTRALIBDEPEND+=$(ACT_HOME)/lib/libphydb.a  $(ACT_HOME)/lib/liblef.a $(ACT_HOME)/lib/libdef.a
endif

ifdef pwroute_INCLUDE
PANDR_PIECES+=-lpwroute 
EXTRALIBDEPEND+=$(ACT_HOME)/lib/libpwroute.a 
endif

ifdef sproute_INCLUDE
PANDR_PIECES+=-lsproute 
EXTRALIBDEPEND+=$(ACT_HOME)/lib/libsproute.a 
endif

ifdef bipart_INCLUDE
PANDR_PIECES+=-lbipart
EXTRALIBDEPEND+=$(ACT_HOME)/lib/libbipart.a 
endif

boost_INCLUDE+=-D_HAS_AUTO_PTR_ETC=0

ALL_INCLUDE=$(boost_INCLUDE) $(galois_INCLUDE) $(galois_eda_INCLUDE) $(dali_INCLUDE) $(phydb_INCLUDE) $(pwroute_INCLUDE) 

ALL_LIBS=$(boost_LIBDIR) $(dali_LIBDIR) $(galois_eda_LIBDIR) $(phydb_LIBDIR) \
	 $(PANDR_PIECES) $(GALOIS_EDA_PIECES) -lverilog_sh -lactchpopt -lactchpsdt -lactchpring -lactchpdecomp -lactchp2prspass -lexpropt_sh $(ACT_HOME)/lib/libabc.so $(DALI_GUI_QT_LIBS)

DFLAGS+=$(ALL_INCLUDE)
CFLAGS+=$(ALL_INCLUDE)
ifeq ($(BASEOS),linux)
CFLAGS+= -pthread
endif


$(EXE): $(OBJS) $(ACTPASSDEPEND) $(SCMCLIDEPEND) $(EXTRALIBDEPEND)
	$(CXX) $(OMPFLAG) $(SH_EXE_OPTIONS) $(CFLAGS) $(OBJS) -o $(EXE) $(SHLIBACTPASS) $(SHLIBASIM) $(LIBACTSCMCLI) $(ALL_LIBS) -ldl -ledit

p2b-test: $(P2B_TEST_EXE)

unit-test: $(UNIT_TEST_EXE)
	./$(UNIT_TEST_EXE)

runtest: unit-test

$(UNIT_TEST_EXE): $(UNIT_TEST_OBJS)
	$(CXX) $(CFLAGS) $(UNIT_TEST_OBJS) -o $@

$(P2B_TEST_EXE): $(P2B_TEST_OBJS) $(ACTPASSDEPEND) $(SCMCLIDEPEND) $(EXTRALIBDEPEND)
	$(CXX) $(OMPFLAG) $(SH_EXE_OPTIONS) $(CFLAGS) $(P2B_TEST_OBJS) -o $@ $(SHLIBACTPASS) $(SHLIBASIM) $(LIBACTSCMCLI) $(ALL_LIBS) -ldl -ledit

placement_cmds.p2btest.o: placement_cmds.cc
	$(CXX) $(CFLAGS) $(CPPFLAGS) -DARCH_$(ARCH) -DOS_$(OS) -DBASEOS_$(BASEOS) -std=$(CPPSTD) -DDALI_P2B_TEST_HARNESS -c $< -o $@

timer_cmds.p2btest.o: timer_cmds.cc
	$(CXX) $(CFLAGS) $(CPPFLAGS) -DARCH_$(ARCH) -DOS_$(OS) -DBASEOS_$(BASEOS) -std=$(CPPSTD) -DDALI_P2B_TEST_HARNESS -c $< -o $@

timing_driven_placement_p2b.p2btest.o: timing_driven_placement_p2b.cc
	$(CXX) $(CFLAGS) $(CPPFLAGS) -DARCH_$(ARCH) -DOS_$(OS) -DBASEOS_$(BASEOS) -std=$(CPPSTD) -DDALI_P2B_TEST_HARNESS -c $< -o $@

-include Makefile.deps
