#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
STUB_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
REPO_ROOT=$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)

if [[ -f "$REPO_ROOT/venv/bin/activate" ]]; then
  # shellcheck disable=SC1091
  source "$REPO_ROOT/venv/bin/activate"
fi

PYGPI_PYTHON_BIN=${PYGPI_PYTHON_BIN:-${VIRTUAL_ENV:+$VIRTUAL_ENV/bin/python}}
PYGPI_PYTHON_BIN=${PYGPI_PYTHON_BIN:-python3}

g++ -std=c++20 -O0 -g0 -DNDEBUG -fPIC -shared \
  -I"$REPO_ROOT/src/cocotb/share/lib/emulator" \
  "$STUB_DIR/smoke_adapter.cpp" \
  -o "$REPO_ROOT/src/cocotb/libs/libemu_adapter.so"

export PYGPI_PYTHON_BIN
export COCOTB_TEST_MODULES=test_smoke_v2
export COCOTB_TOPLEVEL=dut
export COCOTB_TOPLEVEL_LANG=verilog
export GPI_USERS=$("$PYGPI_PYTHON_BIN" -m cocotb_tools.config --pygpi-entry-point)
export EMULATOR_ADAPTER_SO="$REPO_ROOT/src/cocotb/libs/libemu_adapter.so"

if [[ ! -x "$REPO_ROOT/src/cocotb/libs/emulator" ]]; then
  echo "ERROR: emulator binary not found at $REPO_ROOT/src/cocotb/libs/emulator"
  exit 1
fi

"$REPO_ROOT/src/cocotb/libs/emulator"
