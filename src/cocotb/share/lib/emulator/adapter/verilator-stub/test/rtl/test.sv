module test (
    input  logic clk,
    input  logic rst_n,
    output logic [3:0] count_a,
    output logic [4:0] count_b
);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            count_a <= 4'b0000;
        else
            count_a <= count_a + 4'b0001;
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            count_b <= 5'b00000;
        else if (count_b == 5'd30)
            count_b <= 5'd0;
        else
            count_b <= count_b + 5'd1;
    end

endmodule
