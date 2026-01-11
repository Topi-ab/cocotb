module sv_pipeline_2x2 (
    input logic clk_in,
    input logic a_0_in,
    input logic a_1_in,
    output logic b_0_out,
    output logic b_1_out
);
    logic a_0_stage1;
    logic a_1_stage1;
    logic a_0_stage2;
    logic a_1_stage2;

    always_ff @(posedge clk_in) begin
        a_0_stage1 <= a_0_in;
        a_1_stage1 <= a_1_in;
        a_0_stage2 <= a_0_stage1;
        a_1_stage2 <= ~a_1_stage1;
        b_0_out <= a_0_stage2;
        b_1_out <= a_1_stage2;
    end
endmodule
