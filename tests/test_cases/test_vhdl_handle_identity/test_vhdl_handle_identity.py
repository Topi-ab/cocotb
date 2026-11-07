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

import re
from typing import Any

import pytest

import cocotb
from cocotb.handle import HierarchyArrayObject, HierarchyObject, _make_sim_object
from cocotb.triggers import Timer

SIM_NAME = cocotb.SIM_NAME.lower()
IS_GHDL = SIM_NAME.startswith("ghdl")

Spelling = tuple[str, tuple[str, ...], tuple[str, ...]]

# (path as written in the source,
#  other spellings used before iterating the design,
#  other spellings used only after iterating the design)
SPELLINGS: list[Spelling] = [
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
SPELLINGS.append(("gen", ("GEN", "Gen"), ("gEN",)))  # the generate pseudo-region
# GHDL is unable to access signals in generate loops (gh-2594)
if not IS_GHDL:
    SPELLINGS.append(
        ("gen[0].g_sig", ("GEN[0].G_SIG", "Gen[0].G_Sig"), ("gEn[0].g_SIG",))
    )


# extended identifiers that differ only in case: (one, other)
EXTENDED_PAIRS = [("\\Ext_Sig\\", "\\ext_sig\\")]
# the same with a backslash (written twice) in them: \Back\\Slash\ and others
BACKSLASH_NAMES = ["\\Back\\\\Slash\\", "\\back\\\\slash\\", "\\Back\\\\slash\\"]
BACKSLASH_PAIRS = [
    (BACKSLASH_NAMES[0], BACKSLASH_NAMES[1]),
    (BACKSLASH_NAMES[0], BACKSLASH_NAMES[2]),  # differ only after the backslash
]


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
async def case_lookup_before_discovery(dut: Any, spelling: Spelling) -> None:
    """All spellings of a basic identifier give the same handle (no iteration yet)."""
    path, others, _ = spelling
    handle = resolve(dut, path)
    for other in others:
        assert resolve(dut, other) is handle, f"{other} is not {path}"


async def check_distinct(dut: Any, pairs: list[tuple[str, str]]) -> None:
    for one_name, other_name in pairs:
        one = dut[one_name]
        other = dut[other_name]
        assert one is not other, one_name

        one.value = 1
        other.value = 0
        await Timer(1, "ns")
        assert one.value == 1, one_name
        assert other.value == 0, other_name


@cocotb.test
async def extended_identifiers_stay_distinct(dut: Any) -> None:
    """Extended identifiers are case sensitive: \\Ext_Sig\\ and \\ext_sig\\ differ."""
    await check_distinct(dut, EXTENDED_PAIRS)


@cocotb.test
async def extended_identifiers_with_backslash_stay_distinct(dut: Any) -> None:
    """The same with a backslash (written twice) in the extended identifiers."""
    await check_distinct(dut, BACKSLASH_PAIRS)


@cocotb.test
async def extended_identifier_other_case_not_found(dut: Any) -> None:
    """An extended identifier in a case that does not exist is not found."""
    for name in ("\\EXT_SIG\\", "\\BACK\\\\SLASH\\", "\\Back\\\\SLASH\\"):
        with pytest.raises(KeyError):
            dut[name]


def written_once(name: str) -> str:
    """*name*, an extended identifier, with the backslash in it written once."""
    return "\\" + name[1:-1].replace("\\\\", "\\") + "\\"


@cocotb.test
async def extended_identifiers_written_once(dut: Any) -> None:
    """With the backslash in it written once, as some simulators report it, an
    extended identifier gives the same object or is not found, and is not found
    in another case."""
    for name in ("\\BACK\\\\SLASH\\", "\\Back\\\\SLASH\\", "\\back\\\\SLASH\\"):
        with pytest.raises(KeyError):
            dut[written_once(name)]
    for name in BACKSLASH_NAMES:
        try:
            handle = dut[written_once(name)]
        except KeyError:
            continue  # this simulator does not know this form
        assert handle is dut[name], name


@cocotb.test
async def handle_identity_survives_discovery(dut: Any) -> None:
    """Handles looked up by name stay the same objects after iterating the design."""
    before = {path: resolve(dut, path) for path, _, _ in SPELLINGS}
    # first looked up in a spelling that differs from the source
    first_other = dut.FIRST_OTHER
    gen_b = dut.GEN_B
    u_other = dut.U_OTHER
    # an instance, not a generate pseudo-region
    assert isinstance(u_other, HierarchyObject)
    u_other_sig = u_other.SUB_SIG
    assert u_other.sub_sig is u_other_sig
    # GHDL is unable to access signals in generate loops (gh-2594)
    gen_b_1 = None if IS_GHDL else dut.GEN_B[1]
    if gen_b_1 is not None:
        assert gen_b_1.G_SIG is gen_b_1.g_sig

    discover_all(dut)

    for path, handle in before.items():
        assert resolve(dut, path) is handle, f"{path} changed identity"
    # the simulator's iteration of the toplevel, not the cache of the lookups
    iterated = list(dut._handle.iterate(cocotb.simulator.OBJECTS))
    assert first_other._handle in iterated
    assert u_other._handle in iterated
    assert dut.first_other is first_other
    assert dut.u_other.sub_sig is u_other_sig
    if gen_b_1 is not None:
        assert dut.gen_b[1] is gen_b_1
    # GHDL is unable to access signals in generate loops (gh-2594)
    if not IS_GHDL:
        assert gen_b._handle in iterated
        # iterations are only found if their names match the other spelling
        assert sum(1 for _ in gen_b) == 2


@cocotb.test
@cocotb.parametrize(spelling=SPELLINGS)
async def case_lookup_after_discovery(dut: Any, spelling: Spelling) -> None:
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
            # objects that some simulators add to iteration cannot be looked up
            # by name: "#IMPLICIT#" and "s__218_3" (Questa QIS flow, QSIM-84124)
            if child._name.startswith("#") or re.fullmatch(r"s__\d+_\d+", child._name):
                continue
            # ask the simulator, not the cache that the iteration has filled
            found = parent._handle.get_handle_by_name(child._name)
            assert found == child._handle, child._path


@cocotb.test
async def iterated_extended_identifiers_stay_distinct(dut: Any) -> None:
    """Iteration gives one object per extended identifier, however it is reported."""
    found = [
        handle
        for handle in dut._handle.iterate(cocotb.simulator.OBJECTS)
        if "slash" in handle.get_name_string().lower()
    ]
    assert len(found) == len(BACKSLASH_NAMES)
    assert len(set(found)) == len(BACKSLASH_NAMES)
