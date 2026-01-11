import cocotb
from cocotb.handle import _GPISetAction
from cocotb.triggers import Timer


@cocotb.test()
async def emulator_wrapper_ports_smoke(dut):
    """Smoke test using top-level packed ports."""
    def drive_clk(val: int) -> None:
        dut.clk_in._handle.set_signal_val_int(_GPISetAction.DEPOSIT.value, int(val) & 0x1)

    drive_clk(0)
    dut.scan_enable_in.value = 0
    dut.scan_in._handle.set_signal_val_int(_GPISetAction.DEPOSIT.value, 0)
    dut.dut_in.value = 0

    async def tick():
        drive_clk(0)
        await Timer(5, unit="ns")
        drive_clk(1)
        await Timer(5, unit="ns")

    for cycle in range(8):
        dut.dut_in.value = cycle & 0x7
        dut.scan_enable_in.value = 0
        dut.scan_in._handle.set_signal_val_int(_GPISetAction.DEPOSIT.value, 0)

        await tick()

        cocotb.log.info(
            "cycle=%d clk_in=%d dut_in=%d dut_out=%d scan_in=%d scan_out=%d scan_en=%d hash=0x%016x",
            cycle,
            int(dut.clk_in.value),
            int(dut.dut_in.value),
            int(dut.dut_out.value),
            int(dut.scan_in.value),
            int(dut.scan_out.value),
            int(dut.scan_enable_in.value),
            int(dut.dut_hash_out.value),
        )
