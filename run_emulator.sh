#!/bin/bash

set -euo pipefail

source venv/bin/activate

export PYGPI_PYTHON_BIN="$VIRTUAL_ENV/bin/python"
export COCOTB_TEST_MODULES=test_smoke_v2

# Build stub adapter shared library
g++ -std=c++20 -fPIC -shared \
  src/cocotb/share/lib/emulator/adapter_stub.cpp \
  -o src/cocotb/libs/libemu_adapter.so

export EMULATOR_ADAPTER_SO="$PWD/src/cocotb/libs/libemu_adapter.so"

#export COCOTB_TOPLEVEL_LANG=verilog
#export COCOTB_TOPLEVEL=dut
#export PYTHONFAULTHANDLER=1
#export COCOTB_LOG_LEVEL=DEBUG
#export TOPLEVEL_LANG=verilog
#export TOPLEVEL=dut
#export PYTHONPATH=$PWD
#export COCOTB_LOG_LEVEL=DEBUG

src/cocotb/libs/emulator
