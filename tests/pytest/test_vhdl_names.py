# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause

"""Tests of ``VhdlName`` (``src/cocotb/share/lib/gpi/vhdl_names.hpp``) against a
reference model built from the grammar alone (``vhdl_names/``).

The class is compiled into a small shared library with the C++ compiler
(``CXX``, default ``c++``) and called with ctypes. The tests run only when
``COCOTB_TEST_VHDL_NAMES=1``; one CI job is enough, as the class does not
depend on the simulator, the OS or the Python version.

The amount of checking is set with environment variables; the defaults keep a
run short, the values in brackets check much more:

* ``COCOTB_VHDL_NAMES_WIDE_LEN`` (6 [8]) and ``COCOTB_VHDL_NAMES_DEEP_LEN``
  (8 [10]): the maximum length of the strings over the two alphabets
  ``ALPHABET_WIDE`` and ``ALPHABET_DEEP``.
* ``COCOTB_VHDL_NAMES_SAMPLE``: which of these strings are checked: ``all``, a
  number of strings, or a percentage such as ``5%`` (default 20000 [all]).
* ``COCOTB_VHDL_NAMES_RANDOM`` (20000 [1e8]): random strings of length
  ``COCOTB_VHDL_NAMES_RANDOM_LEN`` (16) over ``ALPHABET_WIDE``.
* ``COCOTB_VHDL_NAMES_GENERATED`` (5000 [1e6]): random names generated from the
  LRM grammar, for the properties that need long valid names.
* ``COCOTB_VHDL_NAMES_SEED`` (0x5EED): the seed of everything random.

The ``test_fast_*`` tests run the same checks in C++ on all cores, against a
second implementation of the reference model (PCRE2 on the same grammar); they
are skipped without PCRE2 (``libpcre2-8``, found with ``pkg-config``). Their
amounts: ``COCOTB_VHDL_NAMES_FAST_WIDE_LEN`` (6 [8]), ``_FAST_DEEP_LEN``
(8 [10]), ``_FAST_SAMPLE`` (all), ``_FAST_RANDOM`` (2e7 [5e8]) and
``_FAST_THREADS`` (0: all CPUs of the process).
"""

from __future__ import annotations

import json
import os
import random
import shutil
import subprocess
import sys
from collections.abc import Iterator
from pathlib import Path

import pytest

pytestmark = [
    pytest.mark.skipif(
        os.environ.get("COCOTB_TEST_VHDL_NAMES") != "1",
        reason="set COCOTB_TEST_VHDL_NAMES=1 to test the VHDL name class",
    ),
    pytest.mark.skipif(
        sys.version_info < (3, 11),
        reason="the grammar uses atomic groups (Python 3.11+)",
    ),
]

HERE = Path(__file__).resolve().parent
DATA = HERE / "vhdl_names"
HEADER_DIR = HERE.parents[1] / "src" / "cocotb" / "share" / "lib" / "gpi"

# One byte of every character class the grammar tells apart, and a lower and
# upper case letter for case folding ('\xe4' is a Latin-1 letter, which is not
# folded; '\x01' is not a graphic character).
ALPHABET_WIDE = b"aA0_\\(),'-. \xe4\x01"
# Fewer classes, longer strings: backslashes, indices and character literals.
ALPHABET_DEEP = b"aB\\()',0"

if sys.version_info >= (3, 11):
    sys.path.insert(0, str(DATA))
    import vhdl_names_ref as ref


def env_int(name: str, default: int) -> int:
    return int(float(os.environ.get(name, default)))


SEED = int(os.environ.get("COCOTB_VHDL_NAMES_SEED", "0x5EED"), 0)


def compile_shim(header_dir: Path, out: Path) -> ref.Native:
    cxx = os.environ.get("CXX", "c++")
    if shutil.which(cxx) is None:
        pytest.skip(f"no C++ compiler ({cxx})")
    subprocess.run(
        [
            cxx,
            "-std=c++17",
            "-O2",
            "-shared",
            "-fPIC",
            f"-I{header_dir}",
            str(DATA / "vhdl_names_shim.cpp"),
            "-o",
            str(out),
        ],
        check=True,
    )
    return ref.Native(out)


@pytest.fixture(scope="module")
def native(tmp_path_factory: pytest.TempPathFactory) -> ref.Native:
    return compile_shim(HEADER_DIR, tmp_path_factory.mktemp("vhdl_names") / "shim.so")


# --- the checks of one string -------------------------------------------------


def problem(n: ref.Native, s: bytes) -> str | None:
    """The first thing that is wrong for *s*, or None."""
    for strict in (False, True):
        got, want = n.parse(s, strict), ref.parse(s, strict)
        if got != want:
            return f"strict={strict}: {got} != reference {want}"
    p = n.parse(s)
    if p.lrm and p.kind == "Other":
        return "an LRM name of kind Other"
    key = p.key
    if n.parse(key).key != key:
        return f"key {key!r} is not its own key"
    if p.kind == "Extended":
        source = n.parse(p.source_form)
        if source.kind != "Extended" or source.key != key:
            return f"source form {p.source_form!r} is another name"
        # read back unless the single-backslash form is ambiguous (rule 2)
        if ref.readings(p.single_form) <= 1 and n.parse(p.single_form).key != key:
            return f"single-backslash form {p.single_form!r} is another name"
    if not n.same_identifier(s, s):
        return "same_identifier() is not reflexive"
    # the same text read in both modes
    strict_p = ref.parse(s, True)
    expect = strict_p.valid or s == ref.identifier_key(p, s)
    if (
        n.same_identifier(s, s, True, False) != expect
        or n.same_identifier(s, s, False, True) != expect
    ):
        return "same_identifier() across modes"
    if n.last_component(s) != ref.last_component(s):
        return f"last_component() {n.last_component(s)!r} != reference {ref.last_component(s)!r}"
    msg = helper_problem(n, s, p)
    if msg is not None:
        return msg
    if p.lrm:
        # same key => same meaning: the key is an LRM name meaning the same
        if not ref.is_lrm(key) or ref.meaning(key) != ref.meaning(s):
            return f"key {key!r} means something else"
        # same meaning => same key: the other names with the same meaning are
        # other spellings of the basic identifiers
        if n.parse(ref.respell(s)).key != key:
            return f"other spelling {ref.respell(s)!r} has another key"
    return None


def helper_problem(n: ref.Native, s: bytes, p: ref.Parsed) -> str | None:
    """The helpers the GPI implementations use, for the name *s*."""
    for other in (s, s.upper(), s.lower(), p.single_form, p.source_form):
        want = ref.parse(s).key == ref.parse(other).key
        if n.names_equal(s, other) != want:
            return f"vhdl_names_equal({s!r}, {other!r}) != {want}"
    if not n.helper_same_identifier(s, s):
        return "vhdl_same_identifier() is not reflexive"
    extended = p.kind == "Extended" and ref.last_component(s) == s
    if n.is_extended(s) != extended:
        return f"vhdl_is_extended() != {extended}"
    # the simulator's name is a C string: no NUL in it
    if extended and b"\x00" not in s:
        found = [s, p.source_form, b"top." + s, b":top:" + s, b"/top/" + s, b"", None]
        if ref.readings(p.single_form) <= 1:
            found.append(p.single_form)
        for f in found:
            if n.extended_name_differs(f, s):
                return f"vhdl_extended_name_differs({f!r}, {s!r}) for the same name"
    # independent of the reference's reading of paths: a single name below a
    # parent is keyed as the class keys it (names without separators; a
    # backslash written once before a separator, as in NVC's "\a\.b\", makes
    # the reading of a path ambiguous, and NVC does not find such names)
    if not any(c in s for c in b".:/[\x00"):
        if n.path_key(b"top." + s) != b"TOP." + p.key:
            return f"vhdl_path_key(top.{s!r}) is not TOP. + its key"
    for path in (s, b"top." + s, b":top:" + s, s + b".x", b"top." + s + b"(1)"):
        if n.path_key(path) != ref.path_key(path):
            return f"vhdl_path_key({path!r}) {n.path_key(path)!r} != reference {ref.path_key(path)!r}"
    if s[:1] not in (b".", b":", b"/") and n.component_key(b"." + s) != b"." + p.key:
        return f"vhdl_component_key(.{s!r}) != .key"
    fq = b"top." + s
    if n.child_key(b"TOP", b"top", fq, True) != b"TOP" + ref.path_key(b"." + s):
        return f"vhdl_child_key() of {fq!r}"
    if n.child_key(b"TOP", b"top", fq, False) != b"TOP." + s:
        return f"vhdl_child_key() of a Verilog child {fq!r}"
    if n.child_key(b"K", b"other", fq, True) != fq:
        return "vhdl_child_key() without the parent's prefix"
    return None


def first_problem(n: ref.Native, names) -> str | None:
    for s in names:
        msg = problem(n, s)
        if msg is not None:
            return f"{s!r}: {msg}"
    return None


# --- the strings ----------------------------------------------------------------


def corpus() -> list[bytes]:
    names = []
    for line in (DATA / "corpus.txt").read_text(encoding="latin-1").splitlines():
        if line and not line.startswith("#"):
            h = line.split(" ", 1)[0]
            names.append(b"" if h == "-" else bytes.fromhex(h))
    return names


def all_strings(
    alphabet: bytes, max_len: int, sample: str, seed: int
) -> Iterator[bytes]:
    """Strings over *alphabet* up to *max_len*: all, or a sample (a number of
    strings or a percentage), chosen evenly at random."""
    base = len(alphabet)
    total = sum(base**k for k in range(max_len + 1))
    if sample == "all":
        indices: Iterator[int] | list[int] = iter(range(total))
    else:
        count = (
            int(total * float(sample[:-1]) / 100)
            if sample.endswith("%")
            else int(float(sample))
        )
        indices = sorted(random.Random(seed).sample(range(total), min(count, total)))
    for index in indices:
        rest, length = index, 0
        while rest >= base**length:
            rest -= base**length
            length += 1
        digits = []
        for _ in range(length):
            rest, d = divmod(rest, base)
            digits.append(alphabet[d])
        yield bytes(reversed(digits))


def random_strings(
    alphabet: bytes, length: int, count: int, seed: int
) -> Iterator[bytes]:
    rng = random.Random(seed)
    for _ in range(count):
        yield bytes(rng.choice(alphabet) for _ in range(length))


def generated_names(count: int, seed: int) -> Iterator[bytes]:
    rng = random.Random(seed)
    for _ in range(count):
        s = ref.random_match(ref.NAME, rng)
        yield s
        yield ref.parse(s).single_form  # as some simulators report it


SAMPLE = os.environ.get("COCOTB_VHDL_NAMES_SAMPLE", "20000")


# --- the tests --------------------------------------------------------------------


def test_corpus(native: ref.Native) -> None:
    """Known failures, edge cases and strings that caught mutants: checked first."""
    assert first_problem(native, corpus()) is None


def test_state_machine_is_the_strict_grammar(native: ref.Native) -> None:
    """VhdlName::step() accepts exactly NAME of grammar.txt (the LRM 15.4 and
    15.6 identifiers with a subset of indices): a proof for all lengths and all
    byte values. Keys and relaxed mode are checked by the other tests, by
    enumeration and sampling."""
    table, accepting = native.state_machine()
    assert ref.same_language(table, accepting, ref.NAME) is None


@pytest.mark.parametrize(
    ("alphabet", "env", "default"),
    [
        (ALPHABET_WIDE, "COCOTB_VHDL_NAMES_WIDE_LEN", 6),
        (ALPHABET_DEEP, "COCOTB_VHDL_NAMES_DEEP_LEN", 8),
    ],
    ids=["wide", "deep"],
)
def test_all_strings(
    native: ref.Native, alphabet: bytes, env: str, default: int
) -> None:
    """Every string over the alphabet up to the length (or a sample of them)."""
    names = all_strings(alphabet, env_int(env, default), SAMPLE, SEED)
    assert first_problem(native, names) is None


def test_random_strings(native: ref.Native) -> None:
    """Random strings longer than the exhaustive runs reach."""
    names = random_strings(
        ALPHABET_WIDE,
        env_int("COCOTB_VHDL_NAMES_RANDOM_LEN", 16),
        env_int("COCOTB_VHDL_NAMES_RANDOM", 20000),
        SEED,
    )
    assert first_problem(native, names) is None


def test_generated_names(native: ref.Native) -> None:
    """Long LRM names generated from the grammar, and their single-backslash forms."""
    names = generated_names(env_int("COCOTB_VHDL_NAMES_GENERATED", 5000), SEED)
    assert first_problem(native, names) is None


def test_names_with_the_same_meaning_have_the_same_key(native: ref.Native) -> None:
    """For pairs of generated LRM names: the keys are equal exactly when the
    meanings are (most pairs differ, so each name is also paired with another
    spelling of itself)."""
    rng = random.Random(SEED + 1)
    names = [ref.random_match(ref.NAME, rng) for _ in range(500)]
    for a in names:
        for b in (rng.choice(names), ref.respell(a)):
            same_key = native.parse(a).key == native.parse(b).key
            assert same_key == (ref.meaning(a) == ref.meaning(b)), (a, b)


def test_last_component_examples(native: ref.Native) -> None:
    examples = {
        b"rec.\\Field\\": b"\\Field\\",
        b"/top/sig": b"sig",
        b"\\a.b\\": b"\\a.b\\",
        b"x.\\a.b\\": b"\\a.b\\",
        b"\\a\\\\.b\\": b"\\a\\\\.b\\",
        b"sig": b"sig",
        b":top:u:\\S\\": b"\\S\\",
        b"a.": b"",
    }
    for path, last in examples.items():
        assert native.last_component(path) == last, path


MUTANTS = json.loads((DATA / "mutants.json").read_text())


@pytest.mark.parametrize("mutant", MUTANTS, ids=[m["what"] for m in MUTANTS])
def test_mutant_is_caught(tmp_path: Path, mutant: dict[str, str | bool]) -> None:
    """A deliberately broken class must fail these checks; otherwise they do
    not check what they claim to."""
    source = (HEADER_DIR / "vhdl_names.hpp").read_text()
    old, new = str(mutant["old"]), str(mutant["new"])
    assert old in source, "the mutant no longer applies"
    mutated = source.replace(old, new, 1 if mutant["first_only"] else -1)
    (tmp_path / "vhdl_names.hpp").write_text(mutated)
    n = compile_shim(tmp_path, tmp_path / "shim.so")
    table, accepting = n.state_machine()
    caught = (
        ref.same_language(table, accepting, ref.NAME) is not None
        or first_problem(n, corpus()) is not None
        or first_problem(n, all_strings(ALPHABET_WIDE, 5, "20000", SEED)) is not None
        or first_problem(n, all_strings(ALPHABET_DEEP, 7, "20000", SEED)) is not None
    )
    assert caught


# --- the fast checker (fast/check_vhdl_names.cpp, needs PCRE2) ----------------
#
# The same checks in C++, against a second implementation of the reference
# model (fast/ref_pcre.hpp, PCRE2 on the same grammar.txt), on all cores: for
# long runs. COCOTB_VHDL_NAMES_FAST_WIDE_LEN (6 [8]), _FAST_DEEP_LEN (8 [10]),
# _FAST_SAMPLE (all), _FAST_RANDOM (2e7 [5e8]) and _FAST_THREADS (all CPUs of
# the process) set how much it checks.


@pytest.fixture(scope="module")
def fast_checker(tmp_path_factory: pytest.TempPathFactory) -> Path:
    cxx = os.environ.get("CXX", "c++")
    pkg = shutil.which("pkg-config")
    if shutil.which(cxx) is None or pkg is None:
        pytest.skip("no C++ compiler or pkg-config")
    flags = subprocess.run(
        [pkg, "--cflags", "--libs", "libpcre2-8"],
        check=False,
        capture_output=True,
        text=True,
    )
    if flags.returncode != 0:
        pytest.skip("no PCRE2 (libpcre2-8)")
    exe = tmp_path_factory.mktemp("vhdl_names_fast") / "check_vhdl_names"
    subprocess.run(
        [
            cxx,
            "-std=c++17",
            "-O2",
            "-pthread",
            f"-I{HEADER_DIR}",
            str(DATA / "fast" / "check_vhdl_names.cpp"),
            "-o",
            str(exe),
            *flags.stdout.split(),
        ],
        check=True,
    )
    return exe


def run_fast(exe: Path, *args: str) -> None:
    result = subprocess.run(
        [str(exe), str(DATA / "grammar.txt"), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout[-2000:] + result.stderr[-2000:]


def test_fast_reference_agrees(fast_checker: Path) -> None:
    """The two implementations of the reference model give the same results."""

    def meaning_text(s: bytes) -> str:
        if not ref.is_lrm(s):
            return "-"

        def ident(t: tuple) -> str:
            kind = "ext" if t[0] == "ext" else "basic"
            return f"{kind}{len(t[1])}:{t[1].decode('latin-1')}"

        head, groups = ref.meaning(s)
        out = ident(head)
        for group in groups:
            out += "("
            for it in group:
                out += (
                    f"lit{len(it[1])}:{it[1].decode('latin-1')}"
                    if it[0] == "lit"
                    else ident(it)
                )
            out += ")"
        return out.encode("latin-1").hex()

    names = list(all_strings(ALPHABET_WIDE, 4, "all", SEED)) + list(
        all_strings(ALPHABET_DEEP, 6, "all", SEED)
    )
    result = subprocess.run(
        [str(fast_checker), str(DATA / "grammar.txt"), "dump"],
        input="".join(s.hex() + "\n" for s in names),
        capture_output=True,
        text=True,
        check=True,
    )
    lines = result.stdout.splitlines()
    assert len(lines) == len(names)
    for s, line in zip(names, lines):
        want = ""
        for strict in (False, True):
            p = ref.parse(s, strict)
            want += f"{int(p.valid)}{int(p.lrm)}{ref.KINDS.index(p.kind)} "
            want += "".join(
                x.hex() + " "
                for x in (p.identifier, p.index, p.key, p.source_form, p.single_form)
            )
        want += meaning_text(s) + " "
        assert line == want, s


def test_fast_corpus(fast_checker: Path) -> None:
    run_fast(fast_checker, "corpus", str(DATA / "corpus.txt"))


@pytest.mark.parametrize(
    ("alphabet", "env", "default"),
    [
        (ALPHABET_WIDE, "COCOTB_VHDL_NAMES_FAST_WIDE_LEN", 6),
        (ALPHABET_DEEP, "COCOTB_VHDL_NAMES_FAST_DEEP_LEN", 8),
    ],
    ids=["wide", "deep"],
)
def test_fast_all_strings(
    fast_checker: Path, alphabet: bytes, env: str, default: int
) -> None:
    run_fast(
        fast_checker,
        alphabet.hex(),
        str(env_int(env, default)),
        os.environ.get("COCOTB_VHDL_NAMES_FAST_THREADS", "0"),
        os.environ.get("COCOTB_VHDL_NAMES_FAST_SAMPLE", "all"),
    )


def test_fast_random_strings(fast_checker: Path) -> None:
    run_fast(
        fast_checker,
        "random",
        ALPHABET_WIDE.hex(),
        str(env_int("COCOTB_VHDL_NAMES_RANDOM_LEN", 16)),
        str(env_int("COCOTB_VHDL_NAMES_FAST_RANDOM", 20_000_000)),
        os.environ.get("COCOTB_VHDL_NAMES_FAST_THREADS", "0"),
        hex(SEED),
    )
