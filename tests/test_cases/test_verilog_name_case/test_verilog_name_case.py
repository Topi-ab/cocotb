# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
"""Verilog identifiers are case sensitive.

Objects whose names differ only in case are distinct, and a spelling that does
not exist is not found. Guards case-insensitive handling of VHDL names from
leaking into Verilog.
"""

from __future__ import annotations

from typing import Any

import pytest

import cocotb
from cocotb.triggers import Timer


@cocotb.test
async def names_differing_in_case_are_distinct(dut: Any) -> None:
    """foo and FOO are different objects with independent values."""
    for parent in (dut, dut.u_sub):
        lower = parent["foo" if parent is dut else "sig"]
        upper = parent["FOO" if parent is dut else "SIG"]
        assert lower is not upper

        lower.value = 1
        upper.value = 0
        await Timer(1, "ns")
        assert lower.value == 1
        assert upper.value == 0


@cocotb.test
async def other_case_is_not_found(dut: Any) -> None:
    """A spelling with a different case that does not exist is not found."""
    assert dut.Bar is not None
    with pytest.raises(AttributeError):
        dut.bar  # noqa: B018
    with pytest.raises(AttributeError):
        dut.BAR  # noqa: B018
    with pytest.raises(AttributeError):
        dut.U_SUB  # noqa: B018
