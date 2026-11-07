# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause

"""Reference model of ``VhdlName`` (``src/cocotb/share/lib/gpi/vhdl_names.hpp``).

Everything here is derived from the regular expressions in ``grammar.txt``,
not from the C++ code: strict mode is the LRM grammar, relaxed mode follows the
numbered rules of the header comment. Names are bytes (ISO 8859-1).

Also: the native class through a small C shim (``vhdl_names_shim.cpp``,
loaded with ctypes), and two helpers that work on any regular expression of
the grammar: a state machine (to prove that the class accepts the same
language) and a random generator of matching strings.
"""

from __future__ import annotations

import ctypes
import random
import re
from dataclasses import dataclass
from pathlib import Path

try:
    import re._parser as sre_parse  # Python 3.11+
except ImportError:  # pragma: no cover
    import sre_parse  # type: ignore[no-redef]

HERE = Path(__file__).resolve().parent
KINDS = ("Basic", "Extended", "Other")


def load_grammar(path: Path) -> dict[str, bytes]:
    """The definitions of grammar.txt, with ``${X}`` replaced."""
    defs: dict[str, bytes] = {}
    for line in path.read_text(encoding="latin-1").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        name, _, pattern = line.partition("=")
        pattern = pattern.strip()
        for other, value in defs.items():
            pattern = pattern.replace("${" + other + "}", value.decode("latin-1"))
        assert "${" not in pattern, line
        defs[name.strip()] = pattern.encode("latin-1")
    return defs


GRAMMAR = load_grammar(HERE / "grammar.txt")
NAME = GRAMMAR["NAME"]
STRICT_RE = re.compile(NAME)
BASIC_RE = re.compile(GRAMMAR["BASIC"])
INTEGER_RE = re.compile(GRAMMAR["INTEGER"])
CHAR_RE = re.compile(GRAMMAR["CHAR"])
INDEX_RE = re.compile(GRAMMAR["INDEX"])
ITEM_RE = re.compile(GRAMMAR["ITEM"])
NAME_PARTS_RE = re.compile(GRAMMAR["NAME_PARTS"])
REL_INDEX_RE = re.compile(GRAMMAR["REL_INDEX"])
SOURCE_NAME_RE = re.compile(GRAMMAR["SOURCE_NAME"])
REPORTED_NAME_RE = re.compile(GRAMMAR["REPORTED_NAME"])
SPLIT_NAME_RE = re.compile(GRAMMAR["SPLIT_NAME"])
INDEX_TOKEN_RE = re.compile(GRAMMAR["INDEX_TOKEN"])
XID_RE = re.compile(GRAMMAR["XID"])


@dataclass(frozen=True)
class Parsed:
    valid: bool
    lrm: bool
    kind: str
    identifier: bytes
    index: bytes
    key: bytes
    source_form: bytes
    single_form: bytes


def is_lrm(s: bytes) -> bool:
    return STRICT_RE.fullmatch(s) is not None


def upper(s: bytes) -> bytes:
    return bytes(c - 32 if 0x61 <= c <= 0x7A else c for c in s)


def escape(chars: bytes) -> bytes:
    return b"\\" + chars.replace(b"\\", b"\\\\") + b"\\"


def index_ok(rest: bytes) -> bool:
    return REL_INDEX_RE.fullmatch(rest) is not None


def index_key(index: bytes) -> bytes:
    out = []
    for m in INDEX_TOKEN_RE.finditer(index):
        tok = m.group()
        out.append(tok if len(tok) > 1 else upper(tok))
    return b"".join(out)


def parse(s: bytes, strict: bool = False) -> Parsed:
    """What VhdlName(s, mode) gives."""
    lrm = is_lrm(s)
    valid = lrm or not strict
    kind, ident, index = "Other", s, b""
    if valid:
        if m := SOURCE_NAME_RE.fullmatch(s):
            kind, ident, index = (
                "Extended",
                m.group(1).replace(b"\\\\", b"\\"),
                m.group(2),
            )
        elif m := REPORTED_NAME_RE.fullmatch(s):
            kind, ident, index = "Extended", m.group(1), m.group(2)
        elif s[:1] != b"\\":
            if m := SPLIT_NAME_RE.fullmatch(s):
                ident, index = m.group(1), m.group(2)
            if BASIC_RE.fullmatch(ident):
                kind = "Basic"
    if not valid:
        key = s
    elif kind == "Extended":
        key = escape(ident) + index_key(index)
    else:
        key = upper(ident) + index_key(index)
    if kind == "Extended":
        source, single = escape(ident) + index, b"\\" + ident + b"\\" + index
    else:
        source = single = s
    return Parsed(valid, lrm, kind, ident, index, key, source, single)


def identifier_key(p: Parsed, s: bytes) -> bytes:
    """What VhdlName::same_identifier() compares."""
    if not p.valid:
        return s
    return escape(p.identifier) if p.kind == "Extended" else upper(p.identifier)


def readings(s: bytes) -> int:
    """The number of ways to split *s* (starting with a backslash) into an
    extended identifier and an index in relaxed mode (header comment, rule 2)."""
    if s[:1] != b"\\":
        return 0
    splits = []
    if m := SOURCE_NAME_RE.fullmatch(s):
        splits.append((m.group(1).replace(b"\\\\", b"\\"), m.group(2)))
    for j in range(2, len(s)):
        if s[j : j + 1] == b"\\" and (j + 1 == len(s) or index_ok(s[j + 1 :])):
            split = (s[1:j], s[j + 1 :])
            if split not in splits:
                splits.append(split)
    return len(splits)


def meaning(s: bytes) -> tuple:
    """The meaning of an LRM name, from the grammar alone: two names name the
    same object exactly when their meanings are equal (IEEE 1076-2019, 15.4)."""
    m = NAME_PARTS_RE.fullmatch(s)
    if not m:
        raise ValueError(f"not an LRM name: {s!r}")

    def ident(tok: bytes) -> tuple:
        if tok.startswith(b"\\"):
            return ("ext", tok[1:-1].replace(b"\\\\", b"\\"))
        return ("basic", tok.lower())  # any case mapping will do

    groups = []
    for g in INDEX_RE.finditer(m.group(2)):
        items = []
        for it in ITEM_RE.finditer(g.group()[1:-1]):
            tok = it.group()
            if INTEGER_RE.fullmatch(tok) or CHAR_RE.fullmatch(tok):
                items.append(("lit", tok))
            else:
                items.append(ident(tok))
        groups.append(tuple(items))
    return (ident(m.group(1)), tuple(groups))


def respell(s: bytes) -> bytes:
    """*s* with the case of every ASCII letter outside extended identifiers and
    character literals swapped: the same LRM name."""
    out = []
    pos = 0
    for m in re.finditer(
        GRAMMAR["EXTENDED"] + rb"|" + GRAMMAR["CHAR"] + rb"|[A-Za-z]", s
    ):
        out.append(s[pos : m.start()])
        tok = m.group()
        out.append(tok.swapcase() if len(tok) == 1 else tok)
        pos = m.end()
    out.append(s[pos:])
    return b"".join(out)


def last_component(s: bytes, seps: bytes = b".:/") -> bytes:
    """What VhdlName::last_component(s, ".:/") gives (its comment, written out
    step by step)."""
    start, i, n, at_start, depth = 0, 0, len(s), True, 0
    while i < n:
        c = s[i : i + 1]
        if at_start and c == b"\\":
            e = _xid_end(s, i)
            if e is None:
                break  # the rest is the last component
            i, at_start = e, False
            continue
        at_start = False
        if c == b"'" and i + 2 < n and s[i + 2 : i + 3] == b"'":
            i += 3
            continue
        if c == b"\\" and depth > 0:
            e = _xid_end(s, i)
            if e is not None:
                i = e
                continue
        if c in (b"(", b"["):
            depth += 1
        elif c in (b")", b"]") and depth > 0:
            depth -= 1
        elif depth == 0 and c in seps:
            start, at_start = i + 1, True
        i += 1
    last = s[start:]
    if start > 0 and last[:1] != b"\\" and b"\\" in last:
        # the shortest end that starts with a backslash at the start or after
        # a separator and is one extended identifier
        for j in range(start - 1, -1, -1):
            if (
                s[j : j + 1] == b"\\"
                and (j == 0 or s[j - 1 : j] in (b".", b":", b"/"))
                and parse(s[j:]).kind == "Extended"
            ):
                return s[j:]
    return last


def _xid_end(s: bytes, i: int) -> int | None:
    """End of the extended identifier in source form at s[i], as XID."""
    m = XID_RE.match(s, i)
    return m.end() if m else None


def path_key(path: bytes) -> bytes:
    """What vhdl_path_key() gives: the separators as they are, and of every
    component its key, or for a "[...]" index the key of the name before it
    and the index key of the rest. A
    separator inside "(...)" or "[...]", a character literal or an extended
    identifier does not end a component (the rules of the comment of
    vhdl_path_key(), written out step by step)."""
    out = []
    i, n = 0, len(path)
    while i < n:
        if path[i : i + 1] in (b".", b":", b"/"):
            out.append(path[i : i + 1])
            i += 1
            continue
        start, ident_end = i, None
        if path[i : i + 1] == b"\\":
            e = _xid_end(path, i)
            i = n if e is None else e
            ident_end = i
        depth = 0
        while i < n:
            c = path[i : i + 1]
            if c == b"'" and i + 2 < n and path[i + 2 : i + 3] == b"'":
                i += 3
                continue
            if c == b"\\" and depth > 0:
                e = _xid_end(path, i)
                if e is not None:
                    i = e
                    continue
            if c in (b"(", b"["):
                if depth == 0 and ident_end is None:
                    ident_end = i
                depth += 1
            elif c in (b")", b"]") and depth > 0:
                depth -= 1
            elif depth == 0 and c in (b".", b":", b"/"):
                break
            i += 1
        if (
            ident_end is not None
            and ident_end < i
            and path[ident_end : ident_end + 1] == b"["
        ):
            out.append(parse(path[start:ident_end]).key + index_key(path[ident_end:i]))
        else:
            out.append(parse(path[start:i]).key)
    return b"".join(out)


# --- the native class ---------------------------------------------------------


class Native:
    """VhdlName through vhdl_names_shim.cpp (compiled by the test)."""

    def __init__(self, path: Path) -> None:
        lib = ctypes.CDLL(str(path))
        lib.vn_parse.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
        lib.vn_out.restype = ctypes.POINTER(ctypes.c_char)
        lib.vn_same_identifier.argtypes = [
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
        ]
        lib.vn_last_component.argtypes = [
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_char_p,
        ]
        two = [ctypes.c_char_p, ctypes.c_int] * 2
        lib.vn_names_equal.argtypes = two
        lib.vn_helper_same_identifier.argtypes = two
        lib.vn_extended_name_differs.argtypes = two
        lib.vn_is_extended.argtypes = [ctypes.c_char_p, ctypes.c_int]
        lib.vn_component_key.argtypes = [ctypes.c_char_p, ctypes.c_int]
        lib.vn_path_key.argtypes = [ctypes.c_char_p, ctypes.c_int]
        lib.vn_child_key.argtypes = [ctypes.c_char_p, ctypes.c_int] * 3 + [ctypes.c_int]
        self.lib = lib

    def _out(self, i: int) -> bytes:
        return ctypes.string_at(self.lib.vn_out(i), self.lib.vn_out_len(i))

    def parse(self, s: bytes, strict: bool = False) -> Parsed:
        f = self.lib.vn_parse(s, len(s), 1 if strict else 0)
        o = [self._out(i) for i in range(5)]
        return Parsed(bool(f & 1), bool(f & 2), KINDS[f >> 4], *o)

    def same_identifier(
        self, a: bytes, b: bytes, strict_a: bool = False, strict_b: bool = False
    ) -> bool:
        return bool(
            self.lib.vn_same_identifier(a, len(a), strict_a, b, len(b), strict_b)
        )

    def last_component(self, s: bytes, seps: bytes = b".:/") -> bytes:
        n = self.lib.vn_last_component(s, len(s), seps)
        return self._out(0)[:n]

    # the helpers the GPI implementations use

    def names_equal(self, a: bytes, b: bytes) -> bool:
        return bool(self.lib.vn_names_equal(a, len(a), b, len(b)))

    def helper_same_identifier(self, a: bytes, b: bytes) -> bool:
        return bool(self.lib.vn_helper_same_identifier(a, len(a), b, len(b)))

    def is_extended(self, name: bytes) -> bool:
        return bool(self.lib.vn_is_extended(name, len(name)))

    def extended_name_differs(self, found: bytes | None, name: bytes) -> bool:
        flen = -1 if found is None else len(found)
        return bool(
            self.lib.vn_extended_name_differs(found or b"", flen, name, len(name))
        )

    def component_key(self, s: bytes) -> bytes:
        n = self.lib.vn_component_key(s, len(s))
        return self._out(0)[:n]

    def path_key(self, s: bytes) -> bytes:
        n = self.lib.vn_path_key(s, len(s))
        return self._out(0)[:n]

    def child_key(
        self, parent_key: bytes, parent_full: bytes, fq: bytes, vhdl: bool
    ) -> bytes:
        n = self.lib.vn_child_key(
            parent_key,
            len(parent_key),
            parent_full,
            len(parent_full),
            fq,
            len(fq),
            int(vhdl),
        )
        return self._out(0)[:n]

    def state_machine(self) -> tuple[list[list[int]], set[int]]:
        """The table of VhdlName::step() and its accepting states."""
        n = self.lib.vn_num_states()
        table = [[self.lib.vn_step(st, c) for c in range(256)] for st in range(n)]
        return table, {st for st in range(n) if self.lib.vn_accepting(st)}


# --- regular expressions as state machines and generators ---------------------


def _byte_set(op, av) -> frozenset[int]:
    if op is sre_parse.LITERAL:
        return frozenset([av])
    if op is sre_parse.NOT_LITERAL:
        return frozenset(range(256)) - {av}
    if op is sre_parse.ANY:
        return frozenset(range(256))
    if op is sre_parse.IN:
        chars: set[int] = set()
        negate = False
        for o, a in av:
            if o is sre_parse.NEGATE:
                negate = True
            elif o is sre_parse.LITERAL:
                chars.add(a)
            elif o is sre_parse.RANGE:
                chars.update(range(a[0], a[1] + 1))
            elif o is sre_parse.CATEGORY:
                cat = {
                    sre_parse.CATEGORY_SPACE: b" \t\n\r\f\v",
                    sre_parse.CATEGORY_DIGIT: b"0123456789",
                }.get(a)
                if cat is not None:
                    chars.update(cat)
                elif a is sre_parse.CATEGORY_NOT_SPACE:
                    chars.update(set(range(256)) - set(b" \t\n\r\f\v"))
                else:
                    raise NotImplementedError(a)
            else:
                raise NotImplementedError(o)
        return frozenset(set(range(256)) - chars) if negate else frozenset(chars)
    raise NotImplementedError(op)


class Nfa:
    """A regular expression of the grammar (without atomic groups) as a
    nondeterministic state machine over bytes."""

    def __init__(self, pattern: bytes) -> None:
        self.eps: list[set[int]] = []
        self.edges: list[list[tuple[frozenset[int], int]]] = []
        self.start = self._new()
        self.final = self._build(sre_parse.parse(pattern.decode("latin-1")), self.start)

    def _new(self) -> int:
        self.eps.append(set())
        self.edges.append([])
        return len(self.eps) - 1

    def _build(self, seq, s: int) -> int:
        for op, av in seq:
            if op is sre_parse.SUBPATTERN:
                s = self._build(av[-1], s)
            elif op is sre_parse.BRANCH:
                end = self._new()
                for alt in av[1]:
                    self.eps[self._build(alt, s)].add(end)
                s = end
            elif op in (sre_parse.MAX_REPEAT, sre_parse.MIN_REPEAT):
                lo, hi, sub = av
                for _ in range(lo):
                    s = self._build(sub, s)
                if hi is sre_parse.MAXREPEAT:
                    loop = self._new()
                    self.eps[s].add(loop)
                    self.eps[self._build(sub, loop)].add(loop)
                    s = loop
                else:
                    end = self._new()
                    self.eps[s].add(end)
                    for _ in range(hi - lo):
                        s = self._build(sub, s)
                        self.eps[s].add(end)
                    s = end
            else:
                t = self._new()
                self.edges[s].append((_byte_set(op, av), t))
                s = t
        return s

    def closure(self, states) -> frozenset[int]:
        todo, seen = list(states), set(states)
        while todo:
            for t in self.eps[todo.pop()]:
                if t not in seen:
                    seen.add(t)
                    todo.append(t)
        return frozenset(seen)

    def step(self, states: frozenset[int], c: int) -> frozenset[int]:
        return self.closure(
            {t for st in states for chars, t in self.edges[st] if c in chars}
        )


def same_language(
    table: list[list[int]], accepting: set[int], pattern: bytes
) -> bytes | None:
    """None if the state machine (start state 0) accepts exactly the strings that
    fully match *pattern*, otherwise a shortest string accepted by only one of
    them. The product of the two machines is finite, so this is a proof for
    all lengths and all byte values."""
    nfa = Nfa(pattern)
    start = (0, nfa.closure({nfa.start}))
    seen = {start: b""}
    todo = [start]
    while todo:
        nxt = []
        for a, b in todo:
            word = seen[(a, b)]
            if (a in accepting) != (nfa.final in b):
                return word
            for c in range(256):
                pair = (table[a][c], nfa.step(b, c))
                if pair not in seen:
                    seen[pair] = word + bytes([c])
                    nxt.append(pair)
        todo = nxt
    return None


def random_match(pattern: bytes, rng: random.Random, max_extra: int = 3) -> bytes:
    """A random string that fully matches *pattern* (no atomic groups)."""
    out = bytearray()

    def gen(seq) -> None:
        for op, av in seq:
            if op is sre_parse.SUBPATTERN:
                gen(av[-1])
            elif op is sre_parse.BRANCH:
                gen(rng.choice(av[1]))
            elif op in (sre_parse.MAX_REPEAT, sre_parse.MIN_REPEAT):
                lo, hi, sub = av
                top = (
                    lo + max_extra
                    if hi is sre_parse.MAXREPEAT
                    else min(hi, lo + max_extra)
                )
                for _ in range(rng.randint(lo, top)):
                    gen(sub)
            else:
                out.append(rng.choice(sorted(_byte_set(op, av))))

    gen(sre_parse.parse(pattern.decode("latin-1")))
    return bytes(out)
