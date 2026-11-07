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
IS_NVC = SIM_NAME.startswith("nvc")
IS_RIVIERA = SIM_NAME.startswith("riviera")

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
    (BACKSLASH_NAMES[1], BACKSLASH_NAMES[2]),  # differ only before the backslash
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


async def check_one_hot(handles: list[Any], names: list[str]) -> None:
    """Writing 1 to one object and 0 to the others reaches each one alone."""
    for hot in range(len(handles)):
        for i, h in enumerate(handles):
            h.value = 1 if i == hot else 0
        await Timer(1, "ns")
        assert [int(h.value) for h in handles] == [
            1 if i == hot else 0 for i in range(len(handles))
        ], names[hot]


@cocotb.test
async def extended_identifiers_stay_distinct(dut: Any) -> None:
    """Extended identifiers are case sensitive: \\Ext_Sig\\ and \\ext_sig\\ differ."""
    await check_distinct(dut, EXTENDED_PAIRS)


@cocotb.test
async def extended_identifiers_with_backslash_stay_distinct(dut: Any) -> None:
    """The same with a backslash (written twice) in the extended identifiers."""
    await check_distinct(dut, BACKSLASH_PAIRS)
    await check_one_hot([dut[n] for n in BACKSLASH_NAMES], BACKSLASH_NAMES)


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
            # looking the iterated name up again through the GPI gives the
            # object of the handle store, which iteration and lookup share
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
    # the iterated objects are distinct signals, and the ones found by name
    # iteration lists an object once per spelling it was accessed with
    iterated = list({id(c): c for c in dut if "slash" in c._name.lower()}.values())
    assert len(iterated) == len(BACKSLASH_NAMES)
    await check_one_hot(iterated, [c._name for c in iterated])
    for child in iterated:
        assert dut[child._name] is child, child._name


@cocotb.skipif(
    IS_GHDL, reason="GHDL takes an extended block for a generate pseudo-region"
)
@cocotb.test
async def extended_region_keeps_identity(dut: Any) -> None:
    """A region with an extended name, and a basic name in it, keep their handles."""
    blk = dut["\\Blk\\"]
    sig = blk.b_sig
    assert blk.B_SIG is sig
    discover_all(dut)
    assert dut["\\Blk\\"] is blk
    assert dut["\\Blk\\"].B_Sig is sig


@cocotb.skipif(
    IS_GHDL, reason="GHDL is unable to access signals in generate loops (gh-2594)"
)
@cocotb.skipif(
    IS_RIVIERA,
    reason="Riviera-PRO reports the iterations of an extended generate label as "
    "\\Gen__0\\, so the label is not found",
)
@cocotb.test
async def extended_generate_label(dut: Any) -> None:
    """A generate with an extended label is found only in exactly its case."""
    gen = dut["\\Gen\\"]
    assert gen[0].G_SIG is gen[0].g_sig
    with pytest.raises(KeyError):
        dut["\\GEN\\"]


@cocotb.skipif(IS_GHDL, reason="GHDL does not find this array by name")
@cocotb.test
async def extended_two_dimensional_array(dut: Any) -> None:
    """Indexing an extended two-dimensional array reaches the right elements."""
    arr = dut["\\A\\\\B\\"]
    assert arr[1][0].value == 1
    assert arr[0][0].value == 0


@cocotb.test
async def dotted_path_to_extended_names(dut: Any) -> None:
    """A dotted path to extended names that differ only in case gives two objects."""
    lower = dut["plain_blk.\\foo\\"]
    upper = dut["plain_blk.\\FOO\\"]
    assert lower is not upper
    # NVC and GHDL can match extended identifiers in a path regardless of case,
    # so which signal they give depends on the simulator, not on cocotb
    if not (IS_NVC or IS_GHDL):
        assert (int(lower.value), int(upper.value)) == (0, 1)


@cocotb.skipif(
    IS_GHDL, reason="GHDL takes an extended block for a generate pseudo-region"
)
@cocotb.test
async def dotted_path_between_extended_names(dut: Any) -> None:
    """A dotted path from an extended name to an extended name is the object
    found step by step."""
    assert dut["\\Blk\\.\\Sig\\"] is dut["\\Blk\\"]["\\Sig\\"]
