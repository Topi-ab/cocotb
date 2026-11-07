# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
"""Handles to the same VHDL object must be the same Python object.

VHDL basic identifiers are case insensitive, extended identifiers are not.
Every way of reaching an object (lookup by name in any spelling, iteration of
the parent) must yield the same handle, while distinct objects must stay
distinct.

The tests share one ``dut`` and are order dependent: the tests before
``handle_identity_survives_discovery`` must run before anything iterates the
design, so that lookups reach the simulator instead of the cache filled by
iteration. The spellings used after the design has been iterated are not used
before, so that they are looked up in the simulator too.
"""

from __future__ import annotations

from typing import Any

import pytest

import cocotb
from cocotb.handle import HierarchyArrayObject, HierarchyObject, _make_sim_object
from cocotb.triggers import Timer

SIM_NAME = cocotb.SIM_NAME.lower()
IS_GHDL = SIM_NAME.startswith("ghdl")

# (path as written in the source,
#  other spellings used before iterating the design,
#  other spellings used only after iterating the design)
SPELLINGS = [
    ("data_in", ("DATA_IN", "Data_In"), ("data_IN",)),
    ("Mixed_Case", ("mixed_case", "MIXED_CASE"), ("mIXED_cASE",)),
    ("u_sub", ("U_SUB", "U_Sub"), ("u_SUB",)),
    ("u_sub.sub_sig", ("U_SUB.SUB_SIG", "u_sub.Sub_Sig"), ("U_sUB.sub_SIG",)),
]
# GHDL is unable to access record types (gh-2591)
if not IS_GHDL:
    SPELLINGS.append(
        (
            "rec_sig.field_a",
            ("REC_SIG.FIELD_A", "Rec_Sig.Field_A"),
            ("rec_SIG.field_A",),
        )
    )
# GHDL is unable to access signals in generate loops (gh-2594)
if not IS_GHDL:
    SPELLINGS.append(("gen", ("GEN", "Gen"), ("gEN",)))  # the generate pseudo-region
    SPELLINGS.append(
        ("gen[0].g_sig", ("GEN[0].G_SIG", "Gen[0].G_Sig"), ("gEn[0].g_SIG",))
    )


def resolve(dut: Any, path: str) -> Any:
    """Follow a dotted path with optional ``[index]`` parts using public API."""
    handle = dut
    for part in path.split("."):
        name, _, index = part.partition("[")
        handle = getattr(handle, name)
        if index:
            handle = handle[int(index.rstrip("]"))]
    return handle


def discover_all(handle: Any) -> None:
    """Iterate the whole hierarchy below *handle*."""
    if isinstance(handle, (HierarchyObject, HierarchyArrayObject)):
        for child in handle:
            discover_all(child)


@cocotb.test
async def root_lookup_matches_dut(dut: Any) -> None:
    """The toplevel looked up by name in another spelling is the ``dut`` object."""
    root = cocotb.simulator.get_root_handle(dut._name.upper())
    assert root is not None
    assert _make_sim_object(root) is dut


@cocotb.test
@cocotb.parametrize(spelling=SPELLINGS)
async def case_lookup_before_discovery(dut: Any, spelling: Any) -> None:
    """All spellings of a basic identifier give the same handle (no iteration yet)."""
    path, others, _ = spelling
    handle = resolve(dut, path)
    for other in others:
        assert resolve(dut, other) is handle, f"{other} is not {path}"


@cocotb.test
async def extended_identifiers_stay_distinct(dut: Any) -> None:
    """Extended identifiers are case sensitive: \\Ext_Sig\\ and \\ext_sig\\ differ."""
    upper = dut["\\Ext_Sig\\"]
    lower = dut["\\ext_sig\\"]
    assert upper is not lower

    upper.value = 1
    lower.value = 0
    await Timer(1, "ns")
    assert upper.value == 1
    assert lower.value == 0


@cocotb.test
async def extended_identifier_other_case_not_found(dut: Any) -> None:
    """An extended identifier in a case that does not exist is not found."""
    with pytest.raises(KeyError):
        dut["\\EXT_SIG\\"]


@cocotb.test
async def handle_identity_survives_discovery(dut: Any) -> None:
    """Handles looked up by name stay the same objects after iterating the design."""
    before = {path: resolve(dut, path) for path, _, _ in SPELLINGS}

    discover_all(dut)

    for path, handle in before.items():
        assert resolve(dut, path) is handle, f"{path} changed identity"


@cocotb.test
@cocotb.parametrize(spelling=SPELLINGS)
async def case_lookup_after_discovery(dut: Any, spelling: Any) -> None:
    """All spellings of a basic identifier give the same handle after iteration."""
    discover_all(dut)
    path, others, fresh = spelling
    handle = resolve(dut, path)
    for other in (*others, *fresh):
        assert resolve(dut, other) is handle, f"{other} is not {path}"


@cocotb.test
async def iterated_children_match_lookup(dut: Any) -> None:
    """Every child found by iteration is the object found by name."""
    parents = [dut, dut.u_sub]
    # GHDL is unable to access record types (gh-2591)
    if not IS_GHDL:
        parents.append(dut.rec_sig)
    for parent in parents:
        for child in parent:
            assert getattr(parent, child._name) is child, child._path
