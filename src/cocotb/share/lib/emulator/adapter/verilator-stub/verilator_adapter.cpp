#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "emulator.hpp"
#include "verilated.h"
#include "Vemulator_wrapper.h"

namespace {

enum class SignalKind {
    ClkIn,
    A0In,
    A1In,
    B0Out,
    B1Out,
};

struct SignalHandle {
    SignalKind kind;
};

class VerilatorAdapter {
public:
    const char *name() const { return name_.c_str(); }

    int init() {
        std::puts("[verilator_adapter] init");
        dut_.scan_enable_in = 0;
        dut_.scan_in = 0;
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
            case SignalKind::ClkIn: {
                auto clk = static_cast<uint8_t>(dut_.clk_in);
                clk = static_cast<uint8_t>((clk & ~0x1u) | static_cast<uint8_t>(v01));
                dut_.clk_in = clk;
                break;
            }
            case SignalKind::A0In: {
                auto in = static_cast<uint8_t>(dut_.dut_in);
                in = static_cast<uint8_t>((in & ~0x1u) | static_cast<uint8_t>(v01));
                dut_.dut_in = in;
                break;
            }
            case SignalKind::A1In: {
                auto in = static_cast<uint8_t>(dut_.dut_in);
                in = static_cast<uint8_t>((in & ~0x2u) | static_cast<uint8_t>(v01 << 1));
                dut_.dut_in = in;
                break;
            }
            case SignalKind::B0Out:
            case SignalKind::B1Out:
                return -1;
        }
        dut_.scan_enable_in = 0;
        dut_.scan_in = 0;
        dut_.eval();
        if (debug_) {
            const char *name = (h->kind == SignalKind::ClkIn) ? "clk_in"
                               : (h->kind == SignalKind::A0In) ? "a_0_in"
                               : "a_1_in";
            std::printf("[verilator_adapter] set %s=%d -> clk_in=%u dut_in=%u dut_out=%u\n",
                        name, v01,
                        static_cast<unsigned>(dut_.clk_in),
                        static_cast<unsigned>(dut_.dut_in),
                        static_cast<unsigned>(dut_.dut_out));
        }
        return 0;
    }

    int get_i32(void *sig_handle, int32_t *out) {
        auto *h = static_cast<SignalHandle *>(sig_handle);
        if (!h) return -1;

        int32_t value = 0;
        switch (h->kind) {
            case SignalKind::ClkIn:
                value = (static_cast<uint8_t>(dut_.clk_in) & 0x1u) ? 1 : 0;
                break;
            case SignalKind::A0In:
                value = (static_cast<uint8_t>(dut_.dut_in) & 0x1u) ? 1 : 0;
                break;
            case SignalKind::A1In:
                value = (static_cast<uint8_t>(dut_.dut_in) & 0x2u) ? 1 : 0;
                break;
            case SignalKind::B0Out:
                value = (static_cast<uint8_t>(dut_.dut_out) & 0x1u) ? 1 : 0;
                break;
            case SignalKind::B1Out:
                value = (static_cast<uint8_t>(dut_.dut_out) & 0x2u) ? 1 : 0;
                break;
        }
        if (out) *out = value;
        if (debug_) {
            const char *name = (h->kind == SignalKind::ClkIn) ? "clk_in"
                               : (h->kind == SignalKind::A0In) ? "a_0_in"
                               : (h->kind == SignalKind::A1In) ? "a_1_in"
                               : (h->kind == SignalKind::B0Out) ? "b_0_out"
                               : "b_1_out";
            std::printf("[verilator_adapter] get %s -> %d (clk_in=%u dut_in=%u dut_out=%u)\n",
                        name, value,
                        static_cast<unsigned>(dut_.clk_in),
                        static_cast<unsigned>(dut_.dut_in),
                        static_cast<unsigned>(dut_.dut_out));
        }
        return 0;
    }

private:
    void build_catalog() {
        signals_.clear();
        handles_.clear();
        signals_.reserve(6);
        handles_.reserve(5);

        signals_.push_back(EmuAdapterSignal{
            "dut",
            "dut",
            0,
            nullptr,
            -1,
            true,
        });
        handles_.push_back(SignalHandle{SignalKind::ClkIn});
        signals_.push_back(EmuAdapterSignal{
            "clk_in",
            "dut.clk_in",
            1,
            &handles_.back(),
            0,
            false,
        });
        handles_.push_back(SignalHandle{SignalKind::A0In});
        signals_.push_back(EmuAdapterSignal{
            "a_0_in",
            "dut.a_0_in",
            1,
            &handles_.back(),
            0,
            false,
        });
        handles_.push_back(SignalHandle{SignalKind::A1In});
        signals_.push_back(EmuAdapterSignal{
            "a_1_in",
            "dut.a_1_in",
            1,
            &handles_.back(),
            0,
            false,
        });
        handles_.push_back(SignalHandle{SignalKind::B0Out});
        signals_.push_back(EmuAdapterSignal{
            "b_0_out",
            "dut.b_0_out",
            1,
            &handles_.back(),
            0,
            false,
        });
        handles_.push_back(SignalHandle{SignalKind::B1Out});
        signals_.push_back(EmuAdapterSignal{
            "b_1_out",
            "dut.b_1_out",
            1,
            &handles_.back(),
            0,
            false,
        });

        catalog_.signals = signals_.data();
        catalog_.count = static_cast<int>(signals_.size());
    }

    std::string name_ = "verilator_adapter_stub";
    Vemulator_wrapper dut_;
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
