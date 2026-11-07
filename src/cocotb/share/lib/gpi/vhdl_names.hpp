// Copyright cocotb contributors
// Licensed under the Revised BSD License, see LICENSE for details.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COCOTB_VHDL_NAMES_HPP_
#define COCOTB_VHDL_NAMES_HPP_

#include <cstddef>
#include <string>

/* VHDL basic identifiers are case insensitive, extended identifiers (written
 * between backslashes) are case sensitive (IEEE 1076-2019, 15.4).
 *
 * vhdl_canonical_name() returns a name or hierarchical path with all basic
 * identifiers in upper case and all extended identifiers unchanged, so every
 * spelling of the same object gives the same string. A backslash inside an
 * extended identifier is written twice, which toggles the state twice and so
 * needs no special handling. Character literals (e.g. in an index 'a') are
 * kept as they are.
 *
 * Only ASCII letters are converted, independently of the locale, as names
 * may come from the simulator in any encoding.
 */
inline std::string vhdl_canonical_name(const std::string &name) {
    std::string canonical(name);
    bool extended = false;
    for (std::size_t i = 0; i < canonical.size(); ++i) {
        char &c = canonical[i];
        if (c == '\\') {
            extended = !extended;
        } else if (extended) {
            continue;
        } else if (c == '\'' && i + 2 < canonical.size() &&
                   canonical[i + 2] == '\'') {
            i += 2;  // character literal
        } else if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return canonical;
}

/* Compare two VHDL names or paths following the VHDL case rules. */
inline bool vhdl_names_equal(const std::string &a, const std::string &b) {
    return vhdl_canonical_name(a) == vhdl_canonical_name(b);
}

/* True if *name* is a single extended identifier, e.g. "\Foo\". */
inline bool vhdl_is_extended_identifier(const std::string &name) {
    return name.size() >= 2 && name.front() == '\\' && name.back() == '\\';
}

/* True if the simulator name *found* of an object looked up as the extended
 * identifier *name* is that identifier in exactly the same case. *found* may
 * be prefixed by the path of the parent, e.g. "rec.\Field\" or "/top/\Sig\".
 */
inline bool vhdl_extended_name_matches(const std::string &found,
                                       const std::string &name) {
    if (found.size() < name.size()) return false;
    std::size_t start = found.size() - name.size();
    return found.compare(start, name.size(), name) == 0 &&
           (start == 0 || found[start - 1] == '.' || found[start - 1] == ':' ||
            found[start - 1] == '/');
}

#endif  // COCOTB_VHDL_NAMES_HPP_
