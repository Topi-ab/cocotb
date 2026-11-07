# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause

ifeq ($(TOPLEVEL_LANG),verilog)

COCOTB_TOPLEVEL := verilog_name_case
COCOTB_TEST_MODULES := test_verilog_name_case
VERILOG_SOURCES := verilog_name_case.sv

# cocotb inclusions
include $(shell cocotb-config --makefiles)/Makefile.sim

else
all:
	$(info Skipping simulation as only TOPLEVEL_LANG=verilog is supported)

clean::

endif
