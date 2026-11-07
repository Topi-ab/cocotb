// Copyright cocotb contributors
// Licensed under the Revised BSD License, see LICENSE for details.
// SPDX-License-Identifier: BSD-3-Clause

// Fast checker of VhdlName against the PCRE2 reference (ref_pcre.hpp), the
// same checks as test_vhdl_names.py, on all cores. Run by test_fast_checker.
//   check_vhdl_names <grammar.txt> <alphabet as hex> <max length> [threads]
//   [sample]
//     sample: "all" (default), a number of strings or a percentage ("5%"),
//     spread evenly over all strings up to the length
//   check_vhdl_names <grammar.txt> random <alphabet hex> <length> <count>
//   [threads] [seed] check_vhdl_names <grammar.txt> corpus <corpus.txt>
// Fails fast; with VN_MAX_FAILURES=N it stops at the N-th failure.
#include <sched.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ref_pcre.hpp"
#include "vhdl_names.hpp"

namespace {

/* CPUs this process may run on (as nproc: follows the affinity mask, e.g. a
 * container's cpuset), or all online ones if that is not available. */
unsigned default_threads() {
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof set, &set) == 0) {
        int n = CPU_COUNT(&set);
        if (n > 0) return static_cast<unsigned>(n);
    }
    unsigned n = std::thread::hardware_concurrency();
    return n ? n : 1;
}

std::mutex g_mutex;
std::atomic<long> g_failures{0};

std::string show(const std::string &s) {
    std::string r = "b'";
    for (unsigned char ch : s) {
        if (ch == '\\')
            r += "\\\\";
        else if (ch == '\'')
            r += "\\'";
        else if (ch >= 0x20 && ch < 0x7f)
            r += static_cast<char>(ch);
        else {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02x", ch);
            r += buf;
        }
    }
    return r + "'";
}

/* Fail fast: the first failure ends the run, in every thread. With
 * VN_MAX_FAILURES=N (to collect a corpus), the run goes on and stops at the
 * N-th failure. */
long max_failures() {
    static const long n = [] {
        const char *e = std::getenv("VN_MAX_FAILURES");
        return e ? std::max(1L, std::atol(e)) : 1L;
    }();
    return n;
}

void fail(const std::string &s, const std::string &msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    long n = ++g_failures;
    std::printf("FAIL %s %s\n", show(s).c_str(), msg.c_str());
    if (n >= max_failures()) {
        std::printf("RESULT FAILED (stopped at failure %ld)\n", n);
        std::fflush(stdout);
        std::_Exit(1);
    }
}

ref::Parsed native(const std::string &s, bool strict) {
    VhdlName n(s, strict ? VhdlName::Mode::Strict : VhdlName::Mode::Relaxed);
    return ref::Parsed{n.valid(),
                       n.is_lrm(),
                       static_cast<int>(n.kind()),
                       n.identifier(),
                       n.index(),
                       n.key(),
                       n.source_form(),
                       n.single_backslash_form()};
}

void check_one(const std::string &s, const ref::Model &m, ref::Model::Ctx &c,
               long &lrm_count);

/* A check that throws (e.g. the reference finds a name the class takes for
 * an LRM name ill-formed) is a failure too, not a crash. */
void check(const std::string &s, const ref::Model &m, ref::Model::Ctx &c,
           long &lrm_count) {
    try {
        check_one(s, m, c, lrm_count);
    } catch (const std::exception &e) {
        fail(s, std::string("exception: ") + e.what());
    }
}

void check_one(const std::string &s, const ref::Model &m, ref::Model::Ctx &c,
               long &lrm_count) {
    for (bool strict : {false, true}) {
        ref::Parsed got = native(s, strict), want = m.parse(s, strict, c);
        if (!(got == want))
            fail(s, std::string("differs from reference, strict=") +
                        (strict ? "1" : "0") + " key " + show(got.key) +
                        " vs " + show(want.key) + " kind " +
                        std::to_string(got.kind) + " vs " +
                        std::to_string(want.kind));
    }
    VhdlName n(s);
    if (n.is_lrm() && n.kind() == VhdlName::Kind::Other)
        fail(s, "LRM name of kind Other");
    if (VhdlName(s, VhdlName::Mode::Strict).valid() != n.is_lrm())
        fail(s, "strict valid != lrm");
    const std::string key = n.key();
    if (VhdlName(key).key() != key) fail(s, "key not idempotent");
    if (n.kind() == VhdlName::Kind::Extended) {
        VhdlName src(n.source_form());
        if (src.key() != key) fail(s, "source_form has another key");
        if (src.kind() != VhdlName::Kind::Extended)
            fail(s, "source_form not Extended");
        // Read back unless the single-backslash form is ambiguous (header,
        // rule 2).
        if (m.readings(n.single_backslash_form(), c) <= 1 &&
            VhdlName(n.single_backslash_form()).key() != key)
            fail(s, "single_backslash_form has another key");
    }
    if (!n.same_identifier(n)) fail(s, "same_identifier not reflexive");
    if (VhdlName::last_component(s, ".:/") != m.last_component(s, c))
        fail(s, "last_component differs from reference: " +
                    show(VhdlName::last_component(s, ".:/")) + " vs " +
                    show(m.last_component(s, c)));
    {
        // The same text read in both modes: the identifiers are the same
        // exactly when their keys are (a name that is not valid is compared
        // as it was given).
        VhdlName st(s, VhdlName::Mode::Strict);
        ref::Parsed r = m.parse(s, false, c);
        std::string ik = r.kind == 1 ? ref::Model::escape(r.identifier)
                                     : ref::Model::upper(r.identifier);
        bool expect = st.valid() || s == ik;
        if (st.same_identifier(n) != expect || n.same_identifier(st) != expect)
            fail(s, "same_identifier across modes");
    }
    if (n.is_lrm()) {
        ++lrm_count;
        // same key => same meaning: the key is an LRM name that means the
        // same as the name
        if (!m.is_lrm(key, c))
            fail(s, "key is not an LRM name: " + show(key));
        else if (m.semantic(key, c) != m.semantic(s, c))
            fail(s, "key means something else: " + show(key));
        // same meaning => same key: the only other names with the same
        // meaning are other spellings of the basic identifiers
        std::string other = m.respell(s);
        if (m.semantic(other, c) != m.semantic(s, c))
            fail(s, "respell changed the meaning (checker bug)");
        if (VhdlName(other).key() != key)
            fail(s, "other spelling has another key: " + show(other));
    }
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3 || (argc < 4 && std::string(argv[2]) != "dump") ||
        (std::string(argv[2]) == "random" && argc < 6) ||
        (std::string(argv[2]) == "corpus" && argc < 4)) {
        std::fprintf(stderr,
                     "usage: %s grammar.txt alphabet-hex maxlen [threads]\n",
                     argv[0]);
        return 2;
    }
    ref::Model model(argv[1]);
    if (std::string(argv[2]) == "dump") {
        // Cross-check with ref.py: hex strings in, reference fields out.
        ref::Model::Ctx c;
        char line[4096];
        auto tohex = [](const std::string &v) {
            std::string r;
            char buf[3];
            for (unsigned char ch : v) {
                std::snprintf(buf, sizeof buf, "%02x", ch);
                r += buf;
            }
            return r + " ";
        };
        while (std::fgets(line, sizeof line, stdin)) {
            std::string h(line), s;
            while (!h.empty() && (h.back() == '\n' || h.back() == '\r'))
                h.pop_back();
            for (std::size_t i = 0; i + 1 < h.size(); i += 2)
                s += static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16));
            std::string out;
            for (bool strict : {false, true}) {
                ref::Parsed p = model.parse(s, strict, c);
                out += std::to_string(p.valid) + std::to_string(p.lrm) +
                       std::to_string(p.kind) + " " + tohex(p.identifier) +
                       tohex(p.index) + tohex(p.key) + tohex(p.source_form) +
                       tohex(p.single_form);
            }
            out += model.is_lrm(s, c) ? tohex(model.semantic(s, c)) : "- ";
            std::printf("%s\n", out.c_str());
        }
        return 0;
    }
    if (std::string(argv[2]) == "corpus") {
        // The names of corpus.txt: hex per line ('-' empty), then a note.
        //   exhaust grammar.txt corpus <corpus.txt>
        std::ifstream f(argv[3], std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "cannot read %s\n", argv[3]);
            return 2;
        }
        ref::Model::Ctx c;
        std::string line;
        long n = 0, l = 0;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::string h = line.substr(0, line.find(' ')), s;
            if (h != "-")
                for (std::size_t i = 0; i + 1 < h.size(); i += 2)
                    s += static_cast<char>(
                        std::stoi(h.substr(i, 2), nullptr, 16));
            check(s, model, c, l);
            ++n;
        }
        std::printf(
            "RESULT %s (corpus: %ld names, %ld LRM names, %ld failures)\n",
            g_failures ? "FAILED" : "OK", n, l, static_cast<long>(g_failures));
        return g_failures ? 1 : 0;
    }
    if (std::string(argv[2]) == "random") {
        // A fixed number of random strings of one length over an alphabet:
        //   exhaust grammar.txt random <alphabet hex> <length> <count>
        //   [threads] [seed]
        // Each thread takes count/threads strings from its own stream, so a
        // run is repeatable for the same count, threads and seed.
        std::string ahex = argv[3], alpha;
        for (std::size_t i = 0; i + 1 < ahex.size(); i += 2)
            alpha +=
                static_cast<char>(std::stoi(ahex.substr(i, 2), nullptr, 16));
        const int len = std::atoi(argv[4]);
        const long total = std::atol(argv[5]);
        unsigned nthreads = argc > 6 ? std::atoi(argv[6]) : default_threads();
        if (nthreads == 0) nthreads = default_threads();
        const unsigned long long seed =
            argc > 7 ? std::strtoull(argv[7], nullptr, 0) : 0x5eed;
        std::atomic<long> count{0}, lrm{0};
        auto t0 = std::chrono::steady_clock::now();
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < nthreads; ++t) {
            pool.emplace_back([&, t] {
                ref::Model::Ctx c;
                // splitmix64, one stream per thread
                unsigned long long x = seed + 0x9E3779B97F4A7C15ULL * (t + 1);
                auto next = [&x] {
                    unsigned long long z = (x += 0x9E3779B97F4A7C15ULL);
                    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                    return z ^ (z >> 31);
                };
                std::string str(static_cast<std::size_t>(len), '\0');
                long n = 0, l = 0;
                const long mine =
                    total / nthreads + (t < total % nthreads ? 1 : 0);
                while (n < mine) {
                    for (auto &ch : str) ch = alpha[next() % alpha.size()];
                    check(str, model, c, l);
                    ++n;
                }
                count += n;
                lrm += l;
            });
        }
        for (auto &th : pool) th.join();
        double el =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                .count();
        std::printf(
            "RESULT %s (random, length %d, seed %#llx: %ld strings, %ld LRM "
            "names, "
            "%.0f s, %.1f M/s, %ld failures)\n",
            g_failures ? "FAILED" : "OK", len, seed, static_cast<long>(count),
            static_cast<long>(lrm), el, count / el / 1e6,
            static_cast<long>(g_failures));
        return g_failures ? 1 : 0;
    }
    std::string hex = argv[2], alphabet;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        alphabet += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
    int max_len = std::atoi(argv[3]);
    unsigned threads = argc > 4 ? std::atoi(argv[4]) : default_threads();
    if (threads == 0) threads = default_threads();
    const long base = static_cast<long>(alphabet.size());
    // every stride-th string (in the order of the enumeration) is checked
    long all = 0;
    for (long len = 0, n = 1; len <= max_len; ++len, n *= base) all += n;
    const std::string sample = argc > 5 ? argv[5] : "all";
    long wanted = all;
    if (sample != "all") {
        wanted = sample.back() == '%'
                     ? static_cast<long>(all * std::atof(sample.c_str()) / 100)
                     : std::atol(sample.c_str());
    }
    const long stride =
        wanted >= all ? 1 : std::max(1L, all / std::max(1L, wanted));
    long first_index = 1;  // of the strings of the current length

    long total = 0, lrm_total = 0;
    {
        ref::Model::Ctx c;
        long l = 0;
        check("", model, c, l);
        total = 1;
    }
    for (int len = 1; len <= max_len; ++len) {
        long count = 1;
        for (int i = 0; i < len; ++i) count *= base;
        std::atomic<long> lrm{0};
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t) {
            pool.emplace_back([&, t] {
                ref::Model::Ctx c;
                std::string s(static_cast<std::size_t>(len), '\0');
                long local_lrm = 0;
                // the first index of this thread that is a multiple of stride
                long i0 = (stride - first_index % stride) % stride;
                for (long i = i0 + t * stride; i < count;
                     i += threads * stride) {
                    long v = i;
                    for (int k = len - 1; k >= 0; --k) {
                        s[static_cast<std::size_t>(k)] =
                            alphabet[static_cast<std::size_t>(v % base)];
                        v /= base;
                    }
                    check(s, model, c, local_lrm);
                }
                lrm += local_lrm;
            });
        }
        for (auto &th : pool) th.join();
        first_index += count;
        total += (count + stride - 1) / stride;
        lrm_total += lrm;
        std::printf(
            "length %d: %ld strings (%ld LRM names), total %ld, failures %ld\n",
            len, count, static_cast<long>(lrm), total,
            static_cast<long>(g_failures));
        std::fflush(stdout);
    }
    std::printf("RESULT %s (%ld strings, %ld LRM names, %ld failures)\n",
                g_failures ? "FAILED" : "OK", total, lrm_total,
                static_cast<long>(g_failures));
    return g_failures ? 1 : 0;
}
