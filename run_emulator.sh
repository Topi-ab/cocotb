#!/bin/bash

set -euo pipefail

source venv/bin/activate

export PYGPI_PYTHON_BIN="$VIRTUAL_ENV/bin/python"
export COCOTB_TEST_MODULES=test_smoke_v2
export GPI_USERS=$($PYGPI_PYTHON_BIN -m cocotb_tools.config --pygpi-entry-point)

# Build stub adapter shared library
g++ -std=c++20 -fPIC -shared \
  src/cocotb/share/lib/emulator/adapter_stub.cpp \
  -o src/cocotb/libs/libemu_adapter.so

export EMULATOR_ADAPTER_SO="$PWD/src/cocotb/libs/libemu_adapter.so"

src/cocotb/libs/emulator
