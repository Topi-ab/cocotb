import cocotb
from cocotb.triggers import Timer, RisingEdge, FallingEdge


@cocotb.test()
async def smoke_signal_propagation(dut):
    rise_task = RisingEdge(dut.cpp_to_py)
    dut.py_to_cpp.value = 0
    await rise_task
    assert int(dut.cpp_to_py.value) == 1

    fall_task = FallingEdge(dut.cpp_to_py)
    dut.py_to_cpp.value = 1
    await fall_task
    assert int(dut.cpp_to_py.value) == 0

    for i in range(10):
        dut.py_to_cpp.value = 0
        await Timer(0.5, "ns")
        dut.py_to_cpp.value = 1
        await Timer(0.5, "ns")

#    await Timer(10, "us")
