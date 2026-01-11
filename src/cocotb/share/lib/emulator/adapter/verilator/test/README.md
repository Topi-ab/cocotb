Verilator adapter (fixed signature)
===================================

This folder is the starting point for a Verilator-backed adapter targeting a
fixed SystemVerilog module signature. The top-level module is expected to be
named `emulator_wrapper` and expose:

- `clk_in`      (width N)
- `dut_hash_out` (width 64)
- `dut_out`     (width M)
- `dut_in`      (width K)
- `scan_out`    (width N)
- `scan_in`     (width N)
- `scan_enable_in` (width 1)

Constraints:
- `clk_in`, `scan_in`, and `scan_out` share the same width.
- `dut_in` and `dut_out` widths are independent.

Files:
- `verilator_adapter.cpp`: stub adapter that declares the ABI and signal map.
- `rtl/emulator_wrapper.sv`: example of the expected SV signature.

Next steps:
- Run Verilator on your RTL to generate `Vemulator_wrapper.*`.
- Include `Vemulator_wrapper.h` and wire reads/writes in `verilator_adapter.cpp`.
- Update the width constants in the adapter to match your design.
