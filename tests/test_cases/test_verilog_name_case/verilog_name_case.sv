// Copyright cocotb contributors
// Licensed under the Revised BSD License, see LICENSE for details.
// SPDX-License-Identifier: BSD-3-Clause

// Verilog identifiers are case sensitive: foo and FOO are distinct objects.

`timescale 1ns/1ps

module verilog_name_case_sub;
    logic sig = 1'b0;
    logic SIG = 1'b0;
endmodule

module verilog_name_case;
    logic       foo = 1'b0;
    logic       FOO = 1'b0;
    logic [3:0] Bar = 4'h0;

    verilog_name_case_sub u_sub ();
endmodule
