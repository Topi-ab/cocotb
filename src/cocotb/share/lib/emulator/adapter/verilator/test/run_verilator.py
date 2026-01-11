import os

from cocotb_tools.runner import get_runner


def main() -> None:
    here = os.path.dirname(os.path.abspath(__file__))
    rtl = [os.path.join(here, "rtl", "emulator_wrapper.sv")]
    build_dir = os.path.join(os.path.dirname(here), "tmp", "cocotb-verilator")
    include_dir = os.path.join(build_dir, "include")

    runner = get_runner("verilator")
    if not os.path.isdir(build_dir):
        os.makedirs(build_dir, exist_ok=True)

    skip_build = os.environ.get("COCOTB_SKIP_BUILD") == "1"
    if not skip_build:
        runner.build(
        sources=rtl,
        hdl_toplevel="emulator_wrapper",
            build_dir=build_dir,
            includes=[include_dir],
        )
    extra_env = {}
    pythonpath = os.environ.get("PYTHONPATH", "")
    extra_env["PYTHONPATH"] = f"{here}{os.pathsep}{pythonpath}" if pythonpath else here

    test_module = os.environ.get("COCOTB_TEST_MODULES", "test_bench")
    runner.test(
        hdl_toplevel="emulator_wrapper",
        hdl_toplevel_lang="verilog",
        test_module=test_module,
        build_dir=build_dir,
        test_dir=here,
        extra_env=extra_env,
    )


if __name__ == "__main__":
    main()
