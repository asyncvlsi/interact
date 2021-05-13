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
	act_simfile.o act_vfile.o ptr_manager.o ckt_cmds.o flow.o \
	pandr_cmds.o galois_cmds.o actpin.o

CPPSTD=c++17
SRCS=$(OBJS:.o=.cc)

include $(ACT_HOME)/scripts/Makefile.std
include config.mk

ifdef galois_eda_INCLUDE

GALOIS_PIECES=-lacttpass -lgalois_eda -lgalois_shmem -lcyclone

ifeq ($(BASEOS),linux)
GALOIS_PIECES+=-lnuma
endif

endif

ifdef dali_INCLUDE
DALI_PIECES=-ldalilib -lboost_filesystem -lboost_log_setup -lboost_log

ifeq ($(BASEOS)_$(ARCH),darwin_arm64)
DALI_PIECES+=-lboost_filesystem-mt -lboost_log_setup-mt -lboost_log-mt -lboost_thread-mt
else
DALI_PIECES+=-lboost_filesystem -lboost_log_setup -lboost_log -lboost_thread
endif

endif

ifdef phydb_INCLUDE
PHYDB_PIECES=-lphydb -llef -ldef
endif

ifdef pwroute_INCLUDE
PWROUTE_PIECES=-lpwroute 
endif

ALL_INCLUDE=$(boost_INCLUDE) $(galois_eda_INCLUDE) $(dali_INCLUDE) $(phydb_INCLUDE) $(pwroute_INCLUDE)

ALL_LIBS=$(boost_LIBDIR) $(dali_LIBDIR) $(galois_eda_LIBDIR) $(phydb_LIBDIR) \
	$(GALOIS_PIECES) $(DALI_PIECES) $(PHYDB_PIECES) $(PWROUTE_PIECES)

DFLAGS+=$(ALL_INCLUDE)
CFLAGS+=$(ALL_INCLUDE)
ifeq ($(BASEOS),linux)
CFLAGS+= -pthread
endif

OMPFLAG=-fopenmp
ifeq ($(BASEOS),darwin)
OMPFLAG=-lomp
endif

$(EXE): $(OBJS) $(ACTPASSDEPEND) $(SCMCLIDEPEND)
	$(CXX) $(CFLAGS) $(OBJS) -o $(EXE) $(SHLIBACTPASS) $(SHLIBASIM) $(LIBACTSCMCLI) $(ALL_LIBS) $(OMPFLAG) -ldl -ledit

-include Makefile.deps
