#!/bin/bash

source venv/bin/activate


export PYGPI_PYTHON_BIN="$VIRTUAL_ENV/bin/python"
export COCOTB_TEST_MODULES=test_smoke_v2


#export COCOTB_TOPLEVEL_LANG=verilog
#export COCOTB_TOPLEVEL=dut
#export PYTHONFAULTHANDLER=1
#export COCOTB_LOG_LEVEL=DEBUG
#export TOPLEVEL_LANG=verilog
#export TOPLEVEL=dut
#export PYTHONPATH=$PWD
#export COCOTB_LOG_LEVEL=DEBUG


src/cocotb/libs/emulator
