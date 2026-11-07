// Copyright cocotb contributors
// Licensed under the Revised BSD License, see LICENSE for details.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COCOTB_VHDL_NAMES_HPP_
#define COCOTB_VHDL_NAMES_HPP_

#include <cstddef>
#include <string>

/* The name of one VHDL object relative to its parent: an identifier,
 * optionally followed by indices, e.g. "Data_In", "\Ext\", "gen(3)".
 * Hierarchical paths are not names in this sense (see gpi_get_handle_by_name).
 *
 * IEEE 1076-2019, 15.4: basic identifiers are case insensitive, extended
 * identifiers (between backslashes, a backslash in them written twice) are
 * case sensitive, and an extended identifier is never equal to a basic one.
 * Only ASCII letters are folded. The LRM also folds the other ISO 8859-1
 * letters, but names do not reach the GPI in that encoding.
 *
 * A name is read in one of two modes:
 *
 * - Mode::Strict accepts only names that follow the LRM:
 *
 *     name       ::= identifier { index }
 *     identifier ::= basic | extended
 *     basic      ::= letter { [ "_" ] letter_or_digit }
 *     extended   ::= "\" ( graphic_char_but_backslash | "\\" )+ "\"
 *     index      ::= "(" item { "," item } ")"
 *     item       ::= [ "-" ] digit { [ "_" ] digit } | "'" graphic_char "'"
 *                  | basic | extended
 *
 *   letter is an ISO 8859-1 letter, graphic_char is 0x20-0x7E or 0xA0-0xFF.
 *   Any other name is not valid().
 *
 * - Mode::Relaxed (the default) also accepts the names simulators report,
 *   which do not always follow the LRM, e.g. "gen__0", "#IMPLICIT#", or an
 *   extended identifier with a backslash in it written once ("\A\B\"). Every
 *   string is valid() in this mode:
 *
 *   1. A trailing index is split off if it is a sequence of parenthesized
 *      groups; inside a group, "'c'" and an extended identifier in source
 *      form are skipped. Indices are compared as written (their basic
 *      identifiers folded): "(10)" and "(1_0)" differ, as simulators report
 *      an index in one form.
 *   2. A name starting with a backslash is an extended identifier: in
 *      source form (doubled backslashes) if it is one followed by nothing or
 *      an index, otherwise up to the last backslash that is followed by
 *      nothing or an index, its characters taken as they are (the inner
 *      backslashes written once).
 *      With inner backslashes written once, a name can be ambiguous: it can
 *      be split into an identifier and an index in more than one way. It is
 *      then read as just said, which may not be the identifier the simulator
 *      meant. This needs a backslash in the identifier followed by another
 *      backslash or by "(": "\A\\B\" is read in source form as "A\B", never
 *      as "A\\B", and "\A\(\A\()" as "A" with the index "(\A\()", never as
 *      "A\(\A" with the index "()".
 *   3. Anything else is a basic identifier if it is one, or kind Other.
 *      Other names are compared like basic identifiers (ASCII letters
 *      folded, every other byte as it is).
 *
 * A name that is not valid() (Mode::Strict only) is of kind Other, and is
 * compared as it was given.
 *
 * is_lrm() tells whether the name follows the LRM, in both modes. */
class VhdlName {
  public:
    enum class Mode { Strict, Relaxed };
    enum class Kind { Basic, Extended, Other };

    explicit VhdlName(const std::string &text, Mode mode = Mode::Relaxed)
        : m_text(text), m_lrm(follows_lrm(text)) {
        m_valid = mode == Mode::Relaxed || m_lrm;
        parse();
    }

    /* True if the name was accepted in the mode it was read in. */
    bool valid() const { return m_valid; }
    /* True if the name follows the LRM (Mode::Strict grammar). */
    bool is_lrm() const { return m_lrm; }
    Kind kind() const { return m_kind; }

    /* The name as it was given. */
    const std::string &text() const { return m_text; }
    /* The identifier: for an extended identifier, its characters (without
     * the enclosing backslashes, a backslash in it once); otherwise as
     * written. */
    const std::string &identifier() const { return m_identifier; }
    /* The indices as written, e.g. "(3)", or "". */
    const std::string &index() const { return m_index; }

    /* A string that is equal for two names exactly when they name the same
     * object: basic identifiers (and Other names) in upper case, extended
     * identifiers in source form, indices with their identifiers in upper
     * case. For a name that is not valid(), the name as given. */
    std::string key() const {
        if (!m_valid) return m_text;
        return identifier_key() + index_key(m_index);
    }

    /* The name in LRM source form: an extended identifier with a backslash
     * in it written twice. Otherwise the name as given. */
    std::string source_form() const {
        if (m_kind != Kind::Extended) return m_text;
        return escape(m_identifier) + m_index;
    }

    /* The name with any backslash inside an extended identifier written
     * once, the form some simulators report. */
    std::string single_backslash_form() const {
        if (m_kind != Kind::Extended) return m_text;
        return "\\" + m_identifier + "\\" + m_index;
    }

    /* True if both names have the same identifier, ignoring the indices. */
    bool same_identifier(const VhdlName &other) const {
        return identifier_key() == other.identifier_key();
    }

    bool operator==(const VhdlName &other) const {
        return key() == other.key();
    }
    bool operator!=(const VhdlName &other) const { return !(*this == other); }

    /* The last component of a reported path such as "rec.\Field\" or
     * "/top/sig": the part after the last of *separators* that is not inside
     * an extended identifier in source form. */
    static std::string last_component(const std::string &path,
                                      const std::string &separators) {
        std::size_t start = 0;
        bool component_start = true;
        for (std::size_t i = 0; i < path.size(); ++i) {
            char c = path[i];
            if (component_start && c == '\\') {
                // Skip an extended identifier in source form; without one,
                // the rest is the last component.
                std::size_t end = extended_end(path, i);
                if (end == std::string::npos) break;
                i = end - 1;
                component_start = false;
            } else if (separators.find(c) != std::string::npos) {
                start = i + 1;
                component_start = true;
            } else {
                component_start = false;
            }
        }
        return path.substr(start);
    }

    /* End (one past the closing backslash) of the extended identifier in
     * source form that starts at s[i] == '\\', or npos if there is none.
     * Every reading of an extended identifier goes through here. */
    static std::size_t extended_end(const std::string &s, std::size_t i) {
        std::size_t j = i + 1;
        while (j < s.size()) {
            if (s[j] == '\\') {
                if (j + 1 < s.size() && s[j + 1] == '\\') {
                    j += 2;  // a backslash in the identifier
                    continue;
                }
                return j > i + 1 ? j + 1 : std::string::npos;
            }
            ++j;
        }
        return std::string::npos;
    }

    /* State machine of the Mode::Strict grammar, exposed for testing:
     * step() gives the next state for a character, accepting() whether the
     * name may end in a state. State 0 is the start, kDead rejects. */
    enum State {
        kStart,
        kBasic,        // identifier: after a letter or digit
        kBasicUs,      // identifier: after "_"
        kExt,          // identifier: inside "\...", after a character
        kExtFirst,     // identifier: after the opening backslash
        kExtPair,      // identifier: after "\" + the first of "\\"
        kExtBs,        // identifier: after a backslash inside "\..."
        kIdxItem,      // index: at the start of an item
        kIdxSign,      // index: after "-"
        kIdxInt,       // index: after a digit
        kIdxIntUs,     // index: after "_" in an integer
        kIdxChar1,     // index: after the opening "'"
        kIdxChar2,     // index: after the character of "'c"
        kIdxBasic,     // index: basic identifier, after a letter or digit
        kIdxBasicUs,   // index: basic identifier, after "_"
        kIdxExt,       // index: inside "\...", after a character
        kIdxExtFirst,  // index: after the opening backslash
        kIdxExtPair,   // index: after "\" + the first of "\\"
        kIdxExtBs,     // index: after a backslash inside "\..."
        kIdxItemEnd,   // index: after an item
        kIdxEnd,       // after ")"
        kDead,
        kNumStates
    };

    static bool is_letter(unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= 0xC0 && c != 0xD7 && c != 0xF7);
    }
    static bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
    static bool is_graphic(unsigned char c) {
        return (c >= 0x20 && c <= 0x7E) || c >= 0xA0;
    }

    static int step(int state, unsigned char c) {
        const bool letter = is_letter(c);
        const bool alnum = letter || is_digit(c);
        switch (state) {
            case kStart:
                if (letter) return kBasic;
                if (c == '\\') return kExtFirst;
                return kDead;
            case kBasic:
                if (alnum) return kBasic;
                if (c == '_') return kBasicUs;
                if (c == '(') return kIdxItem;
                return kDead;
            case kBasicUs:
                return alnum ? kBasic : kDead;
            case kExtFirst:
                // A backslash right after the opening one cannot close the
                // identifier (it may not be empty): it starts a "\\".
                if (c == '\\') return kExtPair;
                return is_graphic(c) ? kExt : kDead;
            case kExtPair:
                return c == '\\' ? kExt : kDead;
            case kExt:
                if (c == '\\') return kExtBs;
                return is_graphic(c) ? kExt : kDead;
            case kExtBs:
                // A backslash closed the identifier, unless it is doubled.
                if (c == '\\') return kExt;
                if (c == '(') return kIdxItem;
                return kDead;
            case kIdxItem:
                if (c == '-') return kIdxSign;
                if (is_digit(c)) return kIdxInt;
                if (c == '\'') return kIdxChar1;
                if (letter) return kIdxBasic;
                if (c == '\\') return kIdxExtFirst;
                return kDead;
            case kIdxSign:
                return is_digit(c) ? kIdxInt : kDead;
            case kIdxInt:
                if (is_digit(c)) return kIdxInt;
                if (c == '_') return kIdxIntUs;
                return item_end(c);
            case kIdxIntUs:
                return is_digit(c) ? kIdxInt : kDead;
            case kIdxChar1:
                return is_graphic(c) ? kIdxChar2 : kDead;
            case kIdxChar2:
                return c == '\'' ? kIdxItemEnd : kDead;
            case kIdxBasic:
                if (alnum) return kIdxBasic;
                if (c == '_') return kIdxBasicUs;
                return item_end(c);
            case kIdxBasicUs:
                return alnum ? kIdxBasic : kDead;
            case kIdxExtFirst:
                if (c == '\\') return kIdxExtPair;
                return is_graphic(c) ? kIdxExt : kDead;
            case kIdxExtPair:
                return c == '\\' ? kIdxExt : kDead;
            case kIdxExt:
                if (c == '\\') return kIdxExtBs;
                return is_graphic(c) ? kIdxExt : kDead;
            case kIdxExtBs:
                if (c == '\\') return kIdxExt;
                return item_end(c);
            case kIdxItemEnd:
                return item_end(c);
            case kIdxEnd:
                return c == '(' ? kIdxItem : kDead;
            default:
                return kDead;
        }
    }

    static bool accepting(int state) {
        return state == kBasic || state == kExtBs || state == kIdxEnd;
    }

  private:
    static int item_end(unsigned char c) {
        if (c == ',') return kIdxItem;
        if (c == ')') return kIdxEnd;
        return kDead;
    }

    static bool follows_lrm(const std::string &text) {
        int state = kStart;
        for (char c : text) {
            state = step(state, static_cast<unsigned char>(c));
            if (state == kDead) return false;
        }
        return accepting(state);
    }

    static std::string to_upper(const std::string &s) {
        std::string r(s);
        for (char &c : r) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        return r;
    }

    static std::string escape(const std::string &chars) {
        std::string r = "\\";
        for (char c : chars) {
            r += c;
            if (c == '\\') r += '\\';
        }
        return r + "\\";
    }

    /* If *pos* starts a sequence of parenthesized groups that ends the
     * string. */
    static bool is_index(const std::string &s, std::size_t pos) {
        if (pos >= s.size()) return false;
        while (pos < s.size()) {
            if (s[pos] != '(') return false;
            std::size_t i = pos + 1;
            bool closed = false;
            while (i < s.size()) {
                char c = s[i];
                if (c == '\'' && i + 2 < s.size() && s[i + 2] == '\'') {
                    i += 3;
                } else if (c == '\\') {
                    std::size_t end = extended_end(s, i);
                    if (end == std::string::npos) return false;
                    i = end;
                } else if (c == ')') {
                    closed = true;
                    ++i;
                    break;
                } else {
                    ++i;
                }
            }
            if (!closed) return false;
            pos = i;
        }
        return true;
    }

    /* Index key: ASCII letters in upper case, except in "'c'" and "\...\". */
    static std::string index_key(const std::string &index) {
        std::string r;
        std::size_t i = 0;
        while (i < index.size()) {
            char c = index[i];
            if (c == '\'' && i + 2 < index.size() && index[i + 2] == '\'') {
                r.append(index, i, 3);
                i += 3;
            } else if (c == '\\' &&
                       extended_end(index, i) != std::string::npos) {
                std::size_t end = extended_end(index, i);
                r.append(index, i, end - i);
                i = end;
            } else {
                r += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A')
                                            : c;
                ++i;
            }
        }
        return r;
    }

    std::string identifier_key() const {
        if (!m_valid) return m_text;
        if (m_kind == Kind::Extended) return escape(m_identifier);
        return to_upper(m_identifier);
    }

    void parse() {
        const std::string &s = m_text;
        m_kind = Kind::Other;
        m_identifier = s;
        m_index.clear();
        if (!m_valid || s.empty()) return;

        if (s[0] == '\\') {
            // Source form: the first single backslash closes the identifier.
            std::size_t end = extended_end(s, 0);
            if (end != std::string::npos &&
                (end == s.size() || is_index(s, end))) {
                std::string chars;
                for (std::size_t i = 1; i + 1 < end; ++i) {
                    chars += s[i];
                    if (s[i] == '\\') ++i;  // written twice, one backslash
                }
                set(Kind::Extended, chars, s.substr(end));
                return;
            }
            // Reported with inner backslashes written once: the identifier
            // ends at the last backslash that is followed by an index or by
            // nothing.
            for (std::size_t j = s.size() - 1; j >= 2; --j) {
                if (s[j] == '\\' && (j + 1 == s.size() || is_index(s, j + 1))) {
                    set(Kind::Extended, s.substr(1, j - 1), s.substr(j + 1));
                    return;
                }
            }
            return;  // Other
        }

        std::size_t p = s.find('(');
        if (p != std::string::npos && p > 0 && is_index(s, p)) {
            set(Kind::Other, s.substr(0, p), s.substr(p));
        }
        if (follows_lrm(m_identifier)) m_kind = Kind::Basic;
    }

    void set(Kind kind, const std::string &identifier,
             const std::string &index) {
        m_kind = kind;
        m_identifier = identifier;
        m_index = index;
    }

    std::string m_text;
    bool m_lrm;
    bool m_valid;
    Kind m_kind;
    std::string m_identifier;
    std::string m_index;
};

/* Helpers for the GPI implementations, all following the rules of VhdlName. */

/* True if *a* and *b* name the same object (names relative to the parent). */
inline bool vhdl_names_equal(const std::string &a, const std::string &b) {
    return VhdlName(a) == VhdlName(b);
}

/* True if *a* and *b* have the same identifier, e.g. generate labels with and
 * without an index. */
inline bool vhdl_same_identifier(const std::string &a, const std::string &b) {
    return VhdlName(a).same_identifier(VhdlName(b));
}

/* True if *name* is an extended identifier (in source form, or with a
 * backslash in it written once), possibly indexed. */
inline bool vhdl_is_extended(const std::string &name) {
    return VhdlName(name).kind() == VhdlName::Kind::Extended;
}

/* Key of one component appended to a full name: leading separators ('.', ':'
 * or '/') as they are, then the key of the name. */
inline std::string vhdl_component_key(const std::string &component) {
    std::size_t start = component.find_first_not_of(".:/");
    if (start == std::string::npos) return component;
    return component.substr(0, start) + VhdlName(component.substr(start)).key();
}

/* Key of a full name: every component (between '.', ':' or '/' outside an
 * extended identifier) as by vhdl_component_key(). A component may end in a
 * "[...]" index (VPI), whose ASCII letters are folded. */
inline std::string vhdl_path_key(const std::string &path) {
    std::string key;
    std::size_t i = 0;
    while (i < path.size()) {
        std::size_t sep_end = path.find_first_not_of(".:/", i);
        if (sep_end == std::string::npos) {
            key.append(path, i, std::string::npos);
            break;
        }
        key.append(path, i, sep_end - i);
        std::size_t end = sep_end;
        if (path[end] == '\\') {
            std::size_t ext = VhdlName::extended_end(path, end);
            end = ext == std::string::npos ? path.size() : ext;
        }
        end = path.find_first_of(".:/", end);
        if (end == std::string::npos) end = path.size();
        std::string comp = path.substr(sep_end, end - sep_end);
        std::size_t bracket = comp.rfind('[');
        if (bracket != std::string::npos && bracket > 0 && comp.back() == ']') {
            std::string idx = comp.substr(bracket);
            for (char &c : idx)
                if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            key += VhdlName(comp.substr(0, bracket)).key() + idx;
        } else {
            key += VhdlName(comp).key();
        }
        i = end;
    }
    return key;
}

/* Key for the handle store (GpiObjHdl::get_key()) of a child whose full name
 * *fq_name* was built by appending one component to the full name of its
 * parent: the component is added to the parent's key, as its key if it is a
 * VHDL name (*vhdl*; not for a child of a Verilog scope, whose names are case
 * sensitive). Callers build *fq_name* that way; if one does not, the full
 * name is used as the key, as without these rules. */
inline std::string vhdl_child_key(const std::string &parent_key,
                                  const std::string &parent_fullname,
                                  const std::string &fq_name, bool vhdl) {
    if (fq_name.compare(0, parent_fullname.size(), parent_fullname) != 0) {
        return fq_name;
    }
    std::string added = fq_name.substr(parent_fullname.size());
    return parent_key + (vhdl ? vhdl_component_key(added) : added);
}

/* True if the simulator name *found* (possibly prefixed by the path of the
 * parent, e.g. "rec.\Field\" or "/top/\Sig\") of an object looked up as the
 * extended identifier *name* shows that it is another object: its last
 * component is an extended identifier that is not *name* (indices aside), or
 * is reported without backslashes and is not the characters of *name*. No
 * name (NULL or empty) cannot be checked and is not taken as a difference. */
inline bool vhdl_extended_name_differs(const char *found,
                                       const std::string &name) {
    if (!found || !*found) return false;
    VhdlName wanted(name);
    VhdlName reported(VhdlName::last_component(found, ".:/"));
    if (reported.kind() == VhdlName::Kind::Extended) {
        return !reported.same_identifier(wanted);
    }
    return reported.identifier() != wanted.identifier();
}

#endif  // COCOTB_VHDL_NAMES_HPP_
