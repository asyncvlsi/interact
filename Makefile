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

include $(VLSI_TOOLS_SRC)/scripts/Makefile.std
include config.mk

ifdef galois_eda_INCLUDE
GALOIS_PIECES=-lacttpass -lgalois_eda -lgalois_shmem
endif

ifdef dali_INCLUDE
DALI_PIECES=-ldalilib
endif

ifdef phydb_INCLUDE
PHYDB_PIECES=-lphydb
endif

ALL_INCLUDE=$(boost_INCLUDE) $(galois_eda_INCLUDE) $(dali_INCLUDE) $(phydb_INCLUDE)

ALL_LIBS=$(boost_LIBDIR) $(dali_LIBDIR) $(galois_eda_LIBDIR) $(phydb_LIBDIR) \
	$(GALOIS_PIECES) $(DALI_PIECES) $(PHYDB_PIECES)

DFLAGS+=$(ALL_INCLUDE)
CFLAGS+=$(ALL_INCLUDE)

$(EXE): $(OBJS) $(ACTPASSDEPEND) $(SCMCLIDEPEND)
	$(CXX) $(CFLAGS) $(OBJS) -o $(EXE) $(SHLIBACTPASS) $(SHLIBASIM) $(LIBACTSCMCLI) $(ALL_LIBS) -ldl -ledit

-include Makefile.deps
