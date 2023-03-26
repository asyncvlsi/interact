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
	timer_cmds.o pandr_cmds.o placement_cmds.o \
	routing_cmds.o

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
GALOIS_EDA_PIECES+=-lnuma
endif

endif

ifdef dali_INCLUDE
DALI_PIECES=-ldalilib -lboost_filesystem -lboost_log_setup -lboost_log
EXTRALIBDEPEND+=$(ACT_HOME)/lib/libdalilib.a

ifeq ($(BASEOS),darwin)
DALI_PIECES+=-lboost_filesystem-mt -lboost_log_setup-mt -lboost_log-mt -lboost_thread-mt
else
DALI_PIECES+=-lboost_filesystem -lboost_log_setup -lboost_log -lboost_thread
endif

ifdef NEED_LIBCXXFS
DALI_PIECES+=-lstdc++fs
endif

endif

PANDR_PIECES=$(DALI_PIECES)

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


ALL_INCLUDE=$(boost_INCLUDE) $(galois_INCLUDE) $(galois_eda_INCLUDE) $(dali_INCLUDE) $(phydb_INCLUDE) $(pwroute_INCLUDE) 

ALL_LIBS=$(boost_LIBDIR) $(dali_LIBDIR) $(galois_eda_LIBDIR) $(phydb_LIBDIR) \
	 $(PANDR_PIECES) $(GALOIS_EDA_PIECES)

DFLAGS+=$(ALL_INCLUDE)
CFLAGS+=$(ALL_INCLUDE)
ifeq ($(BASEOS),linux)
CFLAGS+= -pthread
endif


$(EXE): $(OBJS) $(ACTPASSDEPEND) $(SCMCLIDEPEND) $(EXTRALIBDEPEND)
	$(CXX) $(OMPFLAG) $(SH_EXE_OPTIONS) $(CFLAGS) $(OBJS) -o $(EXE) $(SHLIBACTPASS) $(SHLIBASIM) $(LIBACTSCMCLI) $(ALL_LIBS) -ldl -ledit

-include Makefile.deps
