// Copyright cocotb contributors
// Licensed under the Revised BSD License, see LICENSE for details.
// SPDX-License-Identifier: BSD-3-Clause

// C interface to VhdlName for the tests (loaded with ctypes).

#include <string>

#include "vhdl_names.hpp"

namespace {
std::string g_out[5];

VhdlName::Mode mode(int strict) {
    return strict ? VhdlName::Mode::Strict : VhdlName::Mode::Relaxed;
}
}  // namespace

extern "C" {

int vn_num_states() { return VhdlName::kNumStates; }
int vn_step(int state, int c) {
    return VhdlName::step(state, static_cast<unsigned char>(c));
}
int vn_accepting(int state) { return VhdlName::accepting(state) ? 1 : 0; }

/* Returns bit 0 valid, bit 1 is_lrm, bits 4-5 kind; the strings are then
 * vn_out(i): 0 identifier, 1 index, 2 key, 3 source_form,
 * 4 single_backslash_form. */
int vn_parse(const char *text, int len, int strict) {
    VhdlName n(std::string(text, static_cast<std::size_t>(len)), mode(strict));
    g_out[0] = n.identifier();
    g_out[1] = n.index();
    g_out[2] = n.key();
    g_out[3] = n.source_form();
    g_out[4] = n.single_backslash_form();
    return (n.valid() ? 1 : 0) | (n.is_lrm() ? 2 : 0) |
           (static_cast<int>(n.kind()) << 4);
}

int vn_out_len(int i) { return static_cast<int>(g_out[i].size()); }
const char *vn_out(int i) { return g_out[i].data(); }

int vn_same_identifier(const char *a, int alen, int astrict, const char *b,
                       int blen, int bstrict) {
    VhdlName x(std::string(a, static_cast<std::size_t>(alen)), mode(astrict));
    VhdlName y(std::string(b, static_cast<std::size_t>(blen)), mode(bstrict));
    return x.same_identifier(y) ? 1 : 0;
}

int vn_last_component(const char *path, int len, const char *seps) {
    g_out[0] = VhdlName::last_component(
        std::string(path, static_cast<std::size_t>(len)), seps);
    return static_cast<int>(g_out[0].size());
}
}
