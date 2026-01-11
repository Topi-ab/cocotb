import cocotb
from cocotb.handle import _GPISetAction
from cocotb.triggers import Event, ReadOnly, Timer, ValueChange


class DutModel:
    def __init__(self, dut, model_ev: Event) -> None:
        self.a_0_stage1 = 0
        self.a_1_stage1 = 0
        self.a_0_stage2 = 0
        self.a_1_stage2 = 0
        self.b_0_out = 0
        self.b_1_out = 0
        self._dut = dut
        self._model_ev = model_ev
        cocotb.start_soon(self.run())

    def step(self, a_0_in: int, a_1_in: int) -> None:
        self.b_0_out = self.a_0_stage2
        self.b_1_out = self.a_1_stage2

        self.a_0_stage2 = self.a_0_stage1
        self.a_1_stage2 = 1 - self.a_1_stage1
        self.a_0_stage1 = a_0_in & 0x1
        self.a_1_stage1 = a_1_in & 0x1

    async def run(self) -> None:
        last_clk = int(self._dut.clk_in.value) & 0x1
        while True:
            await ValueChange(self._dut.clk_in)
            clk = int(self._dut.clk_in.value) & 0x1
            if last_clk == 0 and clk == 1:
                self.step(int(self._dut.a_0_in.value), int(self._dut.a_1_in.value))
                self._model_ev.set()
                self._model_ev.clear()
            last_clk = clk


@cocotb.test()
async def emulator_wrapper_smoke(dut):
    """Template testbench for emulator_wrapper."""
    def drive_clk(val: int) -> None:
        dut.clk_in._handle.set_signal_val_int(_GPISetAction.DEPOSIT.value, int(val))

    drive_clk(0)
    dut.scan_enable_in.value = 0
    dut.scan_in._handle.set_signal_val_int(_GPISetAction.DEPOSIT.value, 0)
    dut.a_0_in.value = 0
    dut.a_1_in.value = 0

    fall_ev = Event()
    model_ev = Event()
    model = DutModel(dut, model_ev)

    async def tick():
        drive_clk(0)
        await Timer(5, unit="ns")
        drive_clk(1)
        await Timer(5, unit="ns")
        fall_ev.set()
        fall_ev.clear()

    async def compare_task():
        while True:
            await fall_ev.wait()
            await ReadOnly()
            if int(dut.scan_enable_in.value) != 0:
                continue
            b0 = int(dut.b_0_out.value)
            b1 = int(dut.b_1_out.value)
            if (b0, b1) != (model.b_0_out, model.b_1_out):
                raise AssertionError(
                    f"mismatch expected=({model.b_0_out},{model.b_1_out}) got=({b0},{b1})"
                )

    cocotb.start_soon(compare_task())

    for _ in range(2):
        await tick()

    for cycle in range(8):
        dut.a_0_in.value = cycle & 0x1
        dut.a_1_in.value = (cycle >> 1) & 0x1
        scan_en = 0
        dut.scan_enable_in.value = scan_en
        dut.scan_in._handle.set_signal_val_int(_GPISetAction.DEPOSIT.value, 0)

        await tick()

        b0 = int(dut.b_0_out.value)
        b1 = int(dut.b_1_out.value)
        cocotb.log.info(
            "cycle=%d a_0_in=%d a_1_in=%d scan_en=%d scan_in=%d -> b_0_out=%d b_1_out=%d",
            cycle,
            int(dut.a_0_in.value),
            int(dut.a_1_in.value),
            int(dut.scan_enable_in.value),
            int(dut.scan_in.value),
            b0,
            b1,
        )
