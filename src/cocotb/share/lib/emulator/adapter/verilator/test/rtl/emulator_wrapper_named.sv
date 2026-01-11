module emulator_wrapper_named (
    input  logic        clk_in,
    input  logic        a_0_in,
    input  logic        a_1_in,
    output logic        b_0_out,
    output logic        b_1_out,
    input  logic [1:0]  scan_in,
    output logic [1:0]  scan_out,
    input  logic        scan_enable_in,
    output logic [63:0] dut_hash_out
);
    logic [1:0] clk_in_vec;
    logic [2:0] dut_in;
    logic [2:0] dut_out;

    // MSBs are intentionally unused to keep vectors, not scalars.
    assign clk_in_vec = {1'b0, clk_in};
    assign dut_in = {1'b0, a_1_in, a_0_in};
    assign b_0_out = dut_out[0];
    assign b_1_out = dut_out[1];

    emulator_wrapper dut (
        .clk_in(clk_in_vec),
        .dut_hash_out(dut_hash_out),
        .dut_out(dut_out),
        .dut_in(dut_in),
        .scan_out(scan_out),
        .scan_in(scan_in),
        .scan_enable_in(scan_enable_in)
    );
endmodule
