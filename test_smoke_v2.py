import cocotb
from cocotb.triggers import Timer, RisingEdge, FallingEdge, ReadWrite


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

    # Vector signal smoke: bus value and bit access.
    dut.vec.value = 0b1010
    await ReadWrite()
    assert int(dut.vec.value) == 0b1010
    vec = dut.vec.value
    assert int(vec[0]) == 0
    assert int(vec[1]) == 1
    assert int(vec[2]) == 0
    assert int(vec[3]) == 1

    dut.vec.value = 0b1011
    await ReadWrite()
    assert int(dut.vec.value) == 0b1011
