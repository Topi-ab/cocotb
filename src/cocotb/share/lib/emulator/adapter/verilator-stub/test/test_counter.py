import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge


@cocotb.test()
async def counter_counts(dut):
    dut.clk.value = 0
    dut.rst_n.value = 0

    cocotb.start_soon(Clock(dut.clk, 1, unit="ns").start())

    for _ in range(2):
        await RisingEdge(dut.clk)

    dut.rst_n.value = 1

    await RisingEdge(dut.clk)
    assert int(dut.count_a.value) == 1, f"expected count_a=1, got {int(dut.count_a.value)}"
    assert int(dut.count_b.value) == 1, f"expected count_b=1, got {int(dut.count_b.value)}"

    for i in range(2, 6):
        await RisingEdge(dut.clk)
        got = int(dut.count_a.value)
        expected = i & 0xF
        assert got == expected, f"expected count_a={expected}, got {got}"
        got_b = int(dut.count_b.value)
        expected_b = i & 0x1F
        assert got_b == expected_b, f"expected count_b={expected_b}, got {got_b}"
