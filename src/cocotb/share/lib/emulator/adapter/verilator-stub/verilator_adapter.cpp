#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "emulator.hpp"
#include "verilated.h"
#include "Vtest.h"

namespace {

enum class SignalKind {
    Clk,
    RstN,
    CountA,
    CountB,
};

struct SignalHandle {
    SignalKind kind;
};

class VerilatorAdapter {
public:
    const char *name() const { return name_.c_str(); }

    int init() {
        std::puts("[verilator_adapter] init");
        dut_.clk = 0;
        dut_.rst_n = 0;
        build_catalog();
        return 0;
    }

    int shutdown() {
        std::puts("[verilator_adapter] shutdown");
        return 0;
    }

    const EmuAdapterCatalog *catalog() const { return &catalog_; }

    int set_i32(void *sig_handle, int32_t value) {
        auto *h = static_cast<SignalHandle *>(sig_handle);
        if (!h) return -1;

        const int32_t v01 = (value != 0) ? 1 : 0;
        switch (h->kind) {
            case SignalKind::Clk:
                dut_.clk = static_cast<uint8_t>(v01);
                break;
            case SignalKind::RstN:
                dut_.rst_n = static_cast<uint8_t>(v01);
                break;
            case SignalKind::CountA:
            case SignalKind::CountB:
                return -1;
        }
        dut_.eval();
        if (debug_) {
            const char *name = (h->kind == SignalKind::Clk) ? "clk"
                               : "rst_n";
            std::printf("[verilator_adapter] set %s=%d -> clk=%u rst_n=%u count_a=%u count_b=%u\n",
                        name, v01,
                        static_cast<unsigned>(dut_.clk),
                        static_cast<unsigned>(dut_.rst_n),
                        static_cast<unsigned>(dut_.count_a),
                        static_cast<unsigned>(dut_.count_b));
        }
        return 0;
    }

    int get_i32(void *sig_handle, int32_t *out) {
        auto *h = static_cast<SignalHandle *>(sig_handle);
        if (!h) return -1;

        int32_t value = 0;
        switch (h->kind) {
            case SignalKind::Clk:
                value = (static_cast<uint8_t>(dut_.clk) & 0x1u) ? 1 : 0;
                break;
            case SignalKind::RstN:
                value = (static_cast<uint8_t>(dut_.rst_n) & 0x1u) ? 1 : 0;
                break;
            case SignalKind::CountA:
                value = static_cast<int32_t>(static_cast<uint8_t>(dut_.count_a) & 0xFu);
                break;
            case SignalKind::CountB:
                value = static_cast<int32_t>(static_cast<uint8_t>(dut_.count_b) & 0x1Fu);
                break;
        }
        if (out) *out = value;
        if (debug_) {
            const char *name = (h->kind == SignalKind::Clk) ? "clk"
                               : (h->kind == SignalKind::RstN) ? "rst_n"
                               : (h->kind == SignalKind::CountA) ? "count_a"
                               : "count_b";
            std::printf("[verilator_adapter] get %s -> %d (clk=%u rst_n=%u count_a=%u count_b=%u)\n",
                        name, value,
                        static_cast<unsigned>(dut_.clk),
                        static_cast<unsigned>(dut_.rst_n),
                        static_cast<unsigned>(dut_.count_a),
                        static_cast<unsigned>(dut_.count_b));
        }
        return 0;
    }

private:
    void build_catalog() {
        signals_.clear();
        handles_.clear();
        signals_.reserve(5);
        handles_.reserve(4);

        signals_.push_back(EmuAdapterSignal{
            "dut",
            "dut",
            0,
            nullptr,
            -1,
            true,
        });
        handles_.push_back(SignalHandle{SignalKind::Clk});
        signals_.push_back(EmuAdapterSignal{
            "clk",
            "dut.clk",
            1,
            &handles_.back(),
            0,
            false,
        });
        handles_.push_back(SignalHandle{SignalKind::RstN});
        signals_.push_back(EmuAdapterSignal{
            "rst_n",
            "dut.rst_n",
            1,
            &handles_.back(),
            0,
            false,
        });
        handles_.push_back(SignalHandle{SignalKind::CountA});
        signals_.push_back(EmuAdapterSignal{
            "count_a",
            "dut.count_a",
            4,
            &handles_.back(),
            0,
            false,
        });
        handles_.push_back(SignalHandle{SignalKind::CountB});
        signals_.push_back(EmuAdapterSignal{
            "count_b",
            "dut.count_b",
            5,
            &handles_.back(),
            0,
            false,
        });

        catalog_.signals = signals_.data();
        catalog_.count = static_cast<int>(signals_.size());
    }

    std::string name_ = "verilator_adapter_stub";
    Vtest dut_;
    bool debug_ = (std::getenv("EMU_VERILATOR_DEBUG") != nullptr);
    std::vector<EmuAdapterSignal> signals_;
    std::vector<SignalHandle> handles_;
    EmuAdapterCatalog catalog_{nullptr, 0};
};

}  // namespace

extern "C" {

void *emulator_adapter_create() {
    return new VerilatorAdapter();
}

void emulator_adapter_destroy(void *hdl) {
    delete static_cast<VerilatorAdapter *>(hdl);
}

const char *emulator_adapter_name_obj(void *hdl) {
    return static_cast<VerilatorAdapter *>(hdl)->name();
}

int emulator_adapter_init_obj(void *hdl) {
    return static_cast<VerilatorAdapter *>(hdl)->init();
}

int emulator_adapter_shutdown_obj(void *hdl) {
    return static_cast<VerilatorAdapter *>(hdl)->shutdown();
}

const EmuAdapterCatalog *emulator_adapter_catalog(void *hdl) {
    return static_cast<VerilatorAdapter *>(hdl)->catalog();
}

int emulator_adapter_set_i32(void *hdl, void *sig_handle, int32_t value) {
    return static_cast<VerilatorAdapter *>(hdl)->set_i32(sig_handle, value);
}

int emulator_adapter_get_i32(void *hdl, void *sig_handle, int32_t *out) {
    return static_cast<VerilatorAdapter *>(hdl)->get_i32(sig_handle, out);
}

}  // extern "C"
