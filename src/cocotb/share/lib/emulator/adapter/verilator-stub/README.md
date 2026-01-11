Verilator adapter stub
======================

This folder is the starting point for a real Verilator-backed adapter that
implements the emulator adapter ABI from `src/cocotb/share/lib/emulator/emulator.hpp`.

Current state:
- `verilator_adapter.cpp` is a compile-safe stub with TODOs for wiring a real
  Verilator model (`Vtop`) and top-level port access.
- The exported `emulator_adapter_*` symbols are present, but the catalog is
  placeholder-only and does not reflect a real DUT.

Implementation notes:
- The C++ program owns time and clocking. Toggling the clock and calling
  `eval()` is the only way to advance the model.
- All RTL signals you want to drive or sample must be top-level ports in the
  Verilog/SystemVerilog module.
- Add a mapping from adapter signal handles to the Verilator DUT fields and
  update `set_i32` / `get_i32` accordingly.

Once the model is wired:
- Build a shared library (e.g. `libemu_adapter.so`) that exports the
  `emulator_adapter_*` C functions.
- Point `EMULATOR_ADAPTER_SO` to that shared library.
