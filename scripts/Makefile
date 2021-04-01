#-------------------------------------------------------------------------
#
#  Copyright (c) 2011-2019 Rajit Manohar
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
include $(ACT_HOME)/scripts/Makefile.std

install_conf: scm-install

SCM_FILES=prs2net.scm act-opt.scm stk-pass.scm act2lef.scm \
	phydb.scm

scm-install: $(SCM_FILES)
	@if [ ! -d $(INSTALLLIB)/scm ]; then mkdir $(INSTALLLIB)/scm; fi
	@for i in $(SCM_FILES); do $(ACT_HOME)/scripts/install $$i $(INSTALLLIB)/scm/$$i; done

-include Makefile.deps

