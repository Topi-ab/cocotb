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

/* True if *name* is a single extended identifier in source form, e.g. "\Foo\"
 * or "\A\\B\" (a backslash in it is written twice), and not a path such as
 * "\Blk\.\Sig\" or an indexed name such as "\Gen\(0)". */
inline bool vhdl_is_extended_identifier(const std::string &name) {
    if (name.size() < 2 || name.front() != '\\') return false;
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (name[i] != '\\') continue;
        if (i + 1 < name.size() && name[i + 1] == '\\') {
            ++i;  // a backslash in the identifier
            continue;
        }
        return i == name.size() - 1;  // the closing backslash
    }
    return false;
}

/* Canonical form of one component appended to a full name: optional leading
 * separators ('.', ':' or '/') and a name. A name that starts and ends with a
 * backslash is an extended identifier and is kept exactly as it is, as
 * simulators differ in how they report a backslash inside it (written twice
 * or once); anything else goes through vhdl_canonical_name(). */
inline std::string vhdl_canonical_component(const std::string &component) {
    std::size_t start = component.find_first_not_of(".:/");
    if (start == std::string::npos) return component;
    std::string name = component.substr(start);
    if (name.size() >= 2 && name.front() == '\\' && name.back() == '\\') {
        return component;
    }
    return component.substr(0, start) + vhdl_canonical_name(name);
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

/* True if the simulator name *found* of an object looked up as the extended
 * identifier *name* shows that it is another object. *found* may be prefixed
 * by the path of the parent. If its last component is reported as an extended
 * identifier (ends in a backslash), it must be *name* exactly; if it is
 * reported without backslashes, it must be the text of *name* exactly. No name
 * (NULL or empty) cannot be checked and is not taken as a difference. */
inline bool vhdl_extended_name_differs(const char *found,
                                       const std::string &name) {
    if (!found || !*found) return false;
    std::string found_name(found);
    if (vhdl_extended_name_matches(found_name, name)) return false;
    if (found_name.back() == '\\') return true;
    // The text of *name*: without the enclosing backslashes, and a backslash
    // in it written once.
    std::string text;
    for (std::size_t i = 1; i + 1 < name.size(); ++i) {
        text += name[i];
        if (name[i] == '\\' && name[i + 1] == '\\') ++i;
    }
    return !vhdl_extended_name_matches(found_name, text);
}

/* Key for the handle store (GpiObjHdl::get_key()) of a child whose full name
 * *fq_name* was built by appending one component to the full name of its
 * parent: the component is added to the parent's key, in canonical form if it
 * is a VHDL name (*vhdl*; not for a child of a Verilog scope, whose names are
 * case sensitive). Callers build *fq_name* that way; if one does not, the full
 * name is used as the key, as without these rules. */
inline std::string vhdl_child_key(const std::string &parent_key,
                                  const std::string &parent_fullname,
                                  const std::string &fq_name, bool vhdl) {
    if (fq_name.compare(0, parent_fullname.size(), parent_fullname) != 0) {
        return fq_name;
    }
    std::string added = fq_name.substr(parent_fullname.size());
    return parent_key + (vhdl ? vhdl_canonical_component(added) : added);
}

#endif  // COCOTB_VHDL_NAMES_HPP_
