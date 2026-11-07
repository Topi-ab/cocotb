// Copyright cocotb contributors
// Licensed under the Revised BSD License, see LICENSE for details.
// SPDX-License-Identifier: BSD-3-Clause

// Reference model of VhdlName with PCRE2, the same as ../vhdl_names_ref.py:
// the patterns come from ../grammar.txt, the steps follow vhdl_names_ref.py.
#ifndef REF_PCRE_HPP_
#define REF_PCRE_HPP_

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ref {

struct Parsed {
    bool valid, lrm;
    int kind;  // 0 Basic, 1 Extended, 2 Other (as VhdlName::Kind)
    std::string identifier, index, key, source_form, single_form;
    bool operator==(const Parsed &o) const {
        return valid == o.valid && lrm == o.lrm && kind == o.kind &&
               identifier == o.identifier && index == o.index && key == o.key &&
               source_form == o.source_form && single_form == o.single_form;
    }
};

class Regex {
  public:
    Regex() = default;
    Regex(const std::string &pattern, bool full) {
        int err;
        PCRE2_SIZE off;
        // NO_UTF/no UCP: bytes. ENDANCHORED + ANCHORED: fullmatch.
        uint32_t opts = PCRE2_ANCHORED | (full ? PCRE2_ENDANCHORED : 0);
        pcre2_code *code = pcre2_compile(
            reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(),
            opts | PCRE2_NEVER_UTF, &err, &off, nullptr);
        if (!code) {
            PCRE2_UCHAR buf[256];
            pcre2_get_error_message(err, buf, sizeof buf);
            throw std::runtime_error(
                "pcre2: " + std::string(reinterpret_cast<char *>(buf)) +
                " in " + pattern);
        }
        pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
        m_code.reset(code, pcre2_code_free);
    }
    /* Match at *start*; on success the end of the match and the groups. */
    bool match(const std::string &s, std::size_t start, pcre2_match_data *md,
               std::size_t *end = nullptr,
               std::vector<std::string> *groups = nullptr) const {
        int rc =
            pcre2_match(m_code.get(), reinterpret_cast<PCRE2_SPTR>(s.data()),
                        s.size(), start, 0, md, nullptr);
        if (rc < 0) return false;
        PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
        if (end) *end = ov[1];
        if (groups) {
            groups->clear();
            for (int i = 0; i < rc; ++i) {
                if (ov[2 * i] == PCRE2_UNSET)
                    groups->push_back("");
                else
                    groups->push_back(
                        s.substr(ov[2 * i], ov[2 * i + 1] - ov[2 * i]));
            }
        }
        return true;
    }

  private:
    std::shared_ptr<pcre2_code> m_code;
};

inline std::map<std::string, std::string> load_grammar(
    const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    std::map<std::string, std::string> defs;
    std::string line;
    while (std::getline(f, line)) {
        std::size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == '#') continue;
        std::size_t eq = line.find('=');
        std::string name = line.substr(0, eq);
        name = name.substr(0, name.find_last_not_of(" \t") + 1);
        std::string pat = line.substr(eq + 1);
        pat = pat.substr(pat.find_first_not_of(" \t"));
        pat = pat.substr(0, pat.find_last_not_of(" \t\r") + 1);
        for (auto &d : defs) {
            std::string ref = "${" + d.first + "}";
            for (std::size_t p; (p = pat.find(ref)) != std::string::npos;)
                pat.replace(p, ref.size(), d.second);
        }
        if (pat.find("${") != std::string::npos)
            throw std::runtime_error("undefined in " + line);
        defs[name] = pat;
    }
    return defs;
}

class Model {
  public:
    explicit Model(const std::string &grammar_path) {
        auto g = load_grammar(grammar_path);
        strict = Regex(g.at("NAME"), true);
        basic = Regex(g.at("BASIC"), true);
        rel_index = Regex(g.at("REL_INDEX"), true);
        source_name = Regex(g.at("SOURCE_NAME"), true);
        reported_name = Regex(g.at("REPORTED_NAME"), true);
        split_name = Regex(g.at("SPLIT_NAME"), true);
        index_token = Regex(g.at("INDEX_TOKEN"), false);
        name_parts = Regex(g.at("NAME_PARTS"), true);
        index_group = Regex(g.at("INDEX"), false);
        item = Regex(g.at("ITEM"), false);
        integer = Regex(g.at("INTEGER"), true);
        charlit = Regex(g.at("CHAR"), true);
        xid = Regex(g.at("XID"), false);
    }

    /* Per-thread state. */
    struct Ctx {
        pcre2_match_data *md = pcre2_match_data_create(16, nullptr);
        std::vector<std::string> groups;
        Ctx() = default;
        Ctx(const Ctx &) = delete;
        Ctx &operator=(const Ctx &) = delete;
        ~Ctx() { pcre2_match_data_free(md); }
    };

    /* End of an extended identifier in source form at s[i] (XID), or npos. */
    std::size_t xid_end(const std::string &s, std::size_t i, Ctx &c) const {
        std::size_t end;
        return xid.match(s, i, c.md, &end) ? end : std::string::npos;
    }

    /* VhdlName::last_component(s, ".:/"), from its comment, step by step. */
    std::string last_component(const std::string &s, Ctx &c) const {
        std::size_t start = 0, i = 0, n = s.size();
        bool at_start = true;
        int depth = 0;
        auto sep = [](char ch) { return ch == '.' || ch == ':' || ch == '/'; };
        while (i < n) {
            char ch = s[i];
            if (at_start && ch == '\\') {
                std::size_t e = xid_end(s, i, c);
                if (e == std::string::npos) break;
                i = e;
                at_start = false;
                continue;
            }
            at_start = false;
            if (ch == '\'' && i + 2 < n && s[i + 2] == '\'') {
                i += 3;
                continue;
            }
            if (ch == '\\' && depth > 0) {
                std::size_t e = xid_end(s, i, c);
                if (e != std::string::npos) {
                    i = e;
                    continue;
                }
            }
            if (ch == '(' || ch == '[') {
                ++depth;
            } else if ((ch == ')' || ch == ']') && depth > 0) {
                --depth;
            } else if (depth == 0 && sep(ch)) {
                start = i + 1;
                at_start = true;
            }
            ++i;
        }
        std::string last = s.substr(start);
        if (start > 0 && (last.empty() || last[0] != '\\') &&
            last.find('\\') != std::string::npos) {
            for (std::size_t j = start; j-- > 0;) {
                if (s[j] == '\\' && (j == 0 || sep(s[j - 1])) &&
                    parse(s.substr(j), false, c).kind == 1)
                    return s.substr(j);
            }
        }
        return last;
    }

    bool is_lrm(const std::string &s, Ctx &c) const {
        return strict.match(s, 0, c.md);
    }

    static std::string upper(std::string s) {
        for (char &ch : s)
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 32);
        return s;
    }
    static std::string lower(std::string s) {
        for (char &ch : s)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
        return s;
    }
    static std::string escape(const std::string &chars) {
        std::string r = "\\";
        for (char ch : chars) {
            r += ch;
            if (ch == '\\') r += '\\';
        }
        return r + "\\";
    }

    bool index_ok(const std::string &s, std::size_t from, Ctx &c) const {
        return rel_index.match(s, from, c.md);
    }

    std::string index_key(const std::string &index, Ctx &c) const {
        std::string out;
        std::size_t pos = 0, end;
        while (pos < index.size()) {
            if (!index_token.match(index, pos, c.md, &end))
                throw std::logic_error("INDEX_TOKEN");
            std::string tok = index.substr(pos, end - pos);
            out += tok.size() > 1 ? tok : upper(tok);
            pos = end;
        }
        return out;
    }

    Parsed parse(const std::string &s, bool strict_mode, Ctx &c) const {
        Parsed p;
        p.lrm = is_lrm(s, c);
        p.valid = p.lrm || !strict_mode;
        p.kind = 2;
        p.identifier = s;
        p.index.clear();
        if (p.valid) {
            // the rules of grammar.txt, in order
            if (source_name.match(s, 0, c.md, nullptr, &c.groups)) {
                p.kind = 1;
                p.identifier = unescape(c.groups[1]);
                p.index = c.groups[2];
            } else if (reported_name.match(s, 0, c.md, nullptr, &c.groups)) {
                p.kind = 1;
                p.identifier = c.groups[1];
                p.index = c.groups[2];
            } else if (s.empty() || s[0] != '\\') {
                if (split_name.match(s, 0, c.md, nullptr, &c.groups)) {
                    p.identifier = c.groups[1];
                    p.index = c.groups[2];
                }
                if (basic.match(p.identifier, 0, c.md)) p.kind = 0;
            }
        }
        if (!p.valid)
            p.key = s;
        else if (p.kind == 1)
            p.key = escape(p.identifier) + index_key(p.index, c);
        else
            p.key = upper(p.identifier) + index_key(p.index, c);
        if (p.kind == 1) {
            p.source_form = escape(p.identifier) + p.index;
            p.single_form = "\\" + p.identifier + "\\" + p.index;
        } else {
            p.source_form = p.single_form = s;
        }
        return p;
    }

    /* Number of ways to split *s* (starting with a backslash) into an
     * extended identifier and an index in Mode::Relaxed: the source form,
     * and every backslash at 2 or later followed by nothing or an index. */
    int readings(const std::string &s, Ctx &c) const {
        if (s.empty() || s[0] != '\\') return 0;
        std::vector<std::pair<std::string, std::string>> splits;
        if (source_name.match(s, 0, c.md, nullptr, &c.groups)) {
            splits.emplace_back(unescape(c.groups[1]), c.groups[2]);
        }
        for (std::size_t j = 2; j < s.size(); ++j) {
            if (s[j] == '\\' && (j + 1 == s.size() || index_ok(s, j + 1, c))) {
                auto split =
                    std::make_pair(s.substr(1, j - 1), s.substr(j + 1));
                bool seen = false;
                for (auto &x : splits) seen = seen || x == split;
                if (!seen) splits.push_back(split);
            }
        }
        return static_cast<int>(splits.size());
    }

    /* Meaning of an LRM name, from the grammar alone, as a string: two names
     * name the same object exactly when their meanings are equal. */
    std::string semantic(const std::string &s, Ctx &c) const {
        if (!name_parts.match(s, 0, c.md, nullptr, &c.groups))
            throw std::logic_error("semantic of a non-LRM name");
        std::string id = c.groups[1], rest = c.groups[2];
        std::string out = ident(id);
        std::size_t pos = 0, end;
        while (pos < rest.size()) {
            if (!index_group.match(rest, pos, c.md, &end))
                throw std::logic_error("INDEX");
            std::string group = rest.substr(pos + 1, end - pos - 2);
            out += "(";
            std::size_t ip = 0, iend;
            while (ip < group.size()) {
                if (!item.match(group, ip, c.md, &iend))
                    throw std::logic_error("ITEM");
                std::string tok = group.substr(ip, iend - ip);
                if (integer.match(tok, 0, c.md) || charlit.match(tok, 0, c.md))
                    out += field("lit", tok);
                else
                    out += ident(tok);
                ip = iend;
                if (ip < group.size() && group[ip] == ',') ++ip;
            }
            out += ")";
            pos = end;
        }
        return out;
    }

    /* The same name with every ASCII letter outside extended identifiers
     * and character literals in the other case. */
    std::string respell(const std::string &s) const {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            char ch = s[i];
            if (ch == '\\') {
                std::size_t j = i + 1;
                for (; j < s.size(); ++j) {
                    if (s[j] != '\\') continue;
                    if (j + 1 < s.size() && s[j + 1] == '\\') {
                        ++j;
                        continue;
                    }
                    break;
                }
                out.append(s, i, j - i + 1);
                i = j;
            } else if (ch == '\'' && i + 2 < s.size() && s[i + 2] == '\'') {
                out.append(s, i, 3);
                i += 2;
            } else if (ch >= 'a' && ch <= 'z') {
                out += static_cast<char>(ch - 32);
            } else if (ch >= 'A' && ch <= 'Z') {
                out += static_cast<char>(ch + 32);
            } else {
                out += ch;
            }
        }
        return out;
    }

  private:
    static std::string field(const char *tag, const std::string &v) {
        return std::string(tag) + std::to_string(v.size()) + ":" + v;
    }
    static std::string ident(const std::string &tok) {
        if (!tok.empty() && tok[0] == '\\') {
            std::string v;
            for (std::size_t i = 1; i + 1 < tok.size(); ++i) {
                v += tok[i];
                if (tok[i] == '\\') ++i;
            }
            return field("ext", v);
        }
        return field("basic", lower(tok));
    }

    /* The characters of an extended identifier in source form: a backslash
     * written twice is one. */
    static std::string unescape(const std::string &id) {
        std::string v;
        for (std::size_t i = 0; i < id.size(); ++i) {
            v += id[i];
            if (id[i] == '\\') ++i;
        }
        return v;
    }

    Regex strict, basic, rel_index, source_name, reported_name, split_name,
        index_token, name_parts, index_group, item, integer, charlit, xid;
};

}  // namespace ref

#endif  // REF_PCRE_HPP_
