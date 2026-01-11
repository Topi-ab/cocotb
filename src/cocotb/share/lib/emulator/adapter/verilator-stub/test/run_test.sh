#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
STUB_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
RTL_DIR="$SCRIPT_DIR/rtl"
OUT_DIR="$STUB_DIR/tmp"
REPO_ROOT=$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)

echo "RTL source dir: $RTL_DIR"
echo "Output dir: $OUT_DIR"
echo "Step 1/7: create output dir"

mkdir -p "$OUT_DIR"

echo "Step 2/7: run Verilator to generate C++"
verilator -cc --Mdir "$OUT_DIR" \
  "$RTL_DIR/test.sv" \
  --top-module test

echo "Step 3/7: detect VERILATOR_ROOT"
if [[ -z "${VERILATOR_ROOT:-}" ]]; then
  VERILATOR_ROOT=$(verilator -V | sed -n 's/.*VERILATOR_ROOT[[:space:]]*=[[:space:]]*//p' | head -n 1)
fi
if [[ -z "$VERILATOR_ROOT" ]]; then
  if [[ -d /usr/share/verilator ]]; then
    VERILATOR_ROOT=/usr/share/verilator
  else
    echo "ERROR: VERILATOR_ROOT not set and could not be detected."
    exit 1
  fi
fi

echo "VERILATOR_ROOT=$VERILATOR_ROOT"

if [[ -f "$REPO_ROOT/venv/bin/activate" ]]; then
  # shellcheck disable=SC1091
  echo "Step 4/7: activate venv"
  source "$REPO_ROOT/venv/bin/activate"
fi

PYGPI_PYTHON_BIN=${PYGPI_PYTHON_BIN:-${VIRTUAL_ENV:+$VIRTUAL_ENV/bin/python}}
PYGPI_PYTHON_BIN=${PYGPI_PYTHON_BIN:-python3}

echo "Step 5/7: build emulator binary"
if command -v ccache >/dev/null 2>&1; then
  CXX="ccache g++"
  echo "Using ccache"
else
  CXX="g++"
fi

$CXX -std=c++20 -O0 -g0 -DNDEBUG \
  -I"$REPO_ROOT/src/cocotb/share/include" \
  -I"$REPO_ROOT/src/cocotb" \
  -I"$REPO_ROOT/src/cocotb/share/lib/gpi" \
  "$REPO_ROOT/src/cocotb/share/lib/emulator/emulator.cpp" \
  -L"$REPO_ROOT/src/cocotb/libs" -lgpi -ldl \
  -Wl,-rpath,'$ORIGIN' \
  -o "$REPO_ROOT/src/cocotb/libs/emulator"

echo "Step 6/7: build emulator adapter shared library"
$CXX -std=c++20 -O0 -g0 -DNDEBUG -fPIC -shared \
  -I"$VERILATOR_ROOT/include" \
  -I"$OUT_DIR" \
  -I"$REPO_ROOT/src/cocotb/share/lib/emulator" \
  "$VERILATOR_ROOT/include/verilated.cpp" \
  "$VERILATOR_ROOT/include/verilated_threads.cpp" \
  "$STUB_DIR/verilator_adapter.cpp" \
  "$OUT_DIR"/Vtest*.cpp \
  -o "$REPO_ROOT/src/cocotb/libs/libemu_adapter.so"

echo "Step 7/7: run emulator with cocotb test"

export PYGPI_PYTHON_BIN
export COCOTB_TEST_MODULES=test_counter
export COCOTB_TOPLEVEL=dut
export COCOTB_TOPLEVEL_LANG=verilog
export EMU_VERILATOR_DEBUG=1
export EMU_EMULATOR_DEBUG=1
export PYTHONPATH="$SCRIPT_DIR${PYTHONPATH:+:$PYTHONPATH}"
export GPI_USERS=$("$PYGPI_PYTHON_BIN" -m cocotb_tools.config --pygpi-entry-point)
export EMULATOR_ADAPTER_SO="$REPO_ROOT/src/cocotb/libs/libemu_adapter.so"

if [[ ! -x "$REPO_ROOT/src/cocotb/libs/emulator" ]]; then
  echo "ERROR: emulator binary not found at $REPO_ROOT/src/cocotb/libs/emulator"
  exit 1
fi

"$REPO_ROOT/src/cocotb/libs/emulator"
