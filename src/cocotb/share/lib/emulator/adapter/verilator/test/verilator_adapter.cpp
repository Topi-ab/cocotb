#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "emulator.hpp"
#include "verilated.h"
#include "Vemulator_wrapper.h"

namespace {

// Adjust these widths to match the SV signature.
constexpr int kClkWidth = 2;
constexpr int kScanWidth = kClkWidth;
constexpr int kDutInWidth = 3;
constexpr int kDutOutWidth = 3;
constexpr int kDutHashWidth = 64;

enum class SignalKind {
    ClkIn,
    A0In,
    A1In,
    B0Out,
    B1Out,
    ScanIn,
    ScanOut,
    ScanEnable,
    DutHashOut,
};

struct SignalHandle {
    SignalKind kind;
    int index;
};

class VerilatorAdapter {
public:
    const char *name() const { return name_.c_str(); }

    int init() {
        std::puts("[verilator_adapter] init");
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

        const uint32_t mask_clk = (kClkWidth >= 32) ? 0xFFFFFFFFu : ((1u << kClkWidth) - 1u);
        const uint32_t mask_scan = (kScanWidth >= 32) ? 0xFFFFFFFFu : ((1u << kScanWidth) - 1u);
        const int32_t v01 = (value != 0) ? 1 : 0;

        switch (h->kind) {
            case SignalKind::ClkIn:
                dut_.clk_in = static_cast<uint8_t>(value) & mask_clk;
                std::printf("[verilator_adapter] set clk_in=0x%x\n",
                            static_cast<unsigned>(dut_.clk_in));
                break;
            case SignalKind::A0In: {
                auto in = static_cast<uint8_t>(dut_.dut_in);
                in = static_cast<uint8_t>((in & ~0x1u) | static_cast<uint8_t>(v01));
                dut_.dut_in = in;
                std::printf("[verilator_adapter] set a_0_in=%d dut_in=0x%x\n",
                            v01, static_cast<unsigned>(dut_.dut_in));
                break;
            }
            case SignalKind::A1In: {
                auto in = static_cast<uint8_t>(dut_.dut_in);
                in = static_cast<uint8_t>((in & ~0x2u) | static_cast<uint8_t>(v01 << 1));
                dut_.dut_in = in;
                std::printf("[verilator_adapter] set a_1_in=%d dut_in=0x%x\n",
                            v01, static_cast<unsigned>(dut_.dut_in));
                break;
            }
            case SignalKind::ScanIn:
                dut_.scan_in = static_cast<uint8_t>(value) & mask_scan;
                std::printf("[verilator_adapter] set scan_in=0x%x\n",
                            static_cast<unsigned>(dut_.scan_in));
                break;
            case SignalKind::ScanEnable:
                dut_.scan_enable_in = v01;
                std::printf("[verilator_adapter] set scan_enable_in=%d\n", v01);
                break;
            case SignalKind::B0Out:
            case SignalKind::B1Out:
            case SignalKind::ScanOut:
            case SignalKind::DutHashOut:
                return -1;
        }

        dut_.eval();
        std::printf(
            "[verilator_adapter] eval clk_in=0x%x dut_in=0x%x dut_out=0x%x scan_in=0x%x "
            "scan_out=0x%x scan_en=%d hash=0x%016llx\n",
            static_cast<unsigned>(dut_.clk_in),
            static_cast<unsigned>(dut_.dut_in),
            static_cast<unsigned>(dut_.dut_out),
            static_cast<unsigned>(dut_.scan_in),
            static_cast<unsigned>(dut_.scan_out),
            dut_.scan_enable_in ? 1 : 0,
            static_cast<unsigned long long>(dut_.dut_hash_out));
        return 0;
    }

    int get_i32(void *sig_handle, int32_t *out) {
        auto *h = static_cast<SignalHandle *>(sig_handle);
        if (!h) return -1;

        int32_t value = 0;
        switch (h->kind) {
            case SignalKind::ClkIn:
                value = static_cast<uint8_t>(dut_.clk_in) & 0x1u;
                break;
            case SignalKind::A0In:
                value = static_cast<uint8_t>(dut_.dut_in) & 0x1u;
                break;
            case SignalKind::A1In:
                value = (static_cast<uint8_t>(dut_.dut_in) & 0x2u) ? 1 : 0;
                break;
            case SignalKind::B0Out:
                value = static_cast<uint8_t>(dut_.dut_out) & 0x1u;
                break;
            case SignalKind::B1Out:
                value = (static_cast<uint8_t>(dut_.dut_out) & 0x2u) ? 1 : 0;
                break;
            case SignalKind::ScanIn:
                value = static_cast<uint8_t>(dut_.scan_in) & 0x1u;
                break;
            case SignalKind::ScanOut:
                value = static_cast<uint8_t>(dut_.scan_out) & 0x1u;
                break;
            case SignalKind::ScanEnable:
                value = dut_.scan_enable_in ? 1 : 0;
                break;
            case SignalKind::DutHashOut:
                value = static_cast<int32_t>(dut_.dut_hash_out & 0xFFFFFFFFu);
                break;
        }
        if (out) *out = value;
        std::printf("[verilator_adapter] get %d -> %d\n",
                    static_cast<int>(h->kind), value);
        return 0;
    }

private:
    void build_catalog() {
        signals_.clear();
        handles_.clear();

        signals_.push_back(EmuAdapterSignal{
            "dut",
            "dut",
            0,
            nullptr,
            -1,
            true,
        });

        add_bus("clk_in", "dut.clk_in", kClkWidth, SignalKind::ClkIn);
        add_scalar("a_0_in", "dut.a_0_in", SignalKind::A0In);
        add_scalar("a_1_in", "dut.a_1_in", SignalKind::A1In);
        add_bus("scan_in", "dut.scan_in", kScanWidth, SignalKind::ScanIn);
        add_scalar("scan_enable_in", "dut.scan_enable_in", SignalKind::ScanEnable);

        add_scalar("b_0_out", "dut.b_0_out", SignalKind::B0Out);
        add_scalar("b_1_out", "dut.b_1_out", SignalKind::B1Out);
        add_bus("scan_out", "dut.scan_out", kScanWidth, SignalKind::ScanOut);
        add_bus("dut_hash_out", "dut.dut_hash_out", kDutHashWidth, SignalKind::DutHashOut);

        catalog_.signals = signals_.data();
        catalog_.count = static_cast<int>(signals_.size());
    }

    void add_scalar(const char *name, const char *fullname, SignalKind kind) {
        handles_.push_back(SignalHandle{kind, 0});
        signals_.push_back(EmuAdapterSignal{
            name,
            fullname,
            1,
            &handles_.back(),
            0,
            false,
        });
    }

    void add_bus(const char *name, const char *fullname, int width, SignalKind kind) {
        handles_.push_back(SignalHandle{kind, 0});
        signals_.push_back(EmuAdapterSignal{
            name,
            fullname,
            width,
            &handles_.back(),
            0,
            false,
        });
    }

    std::string name_ = "verilator_adapter_stub_fixed";
    Vemulator_wrapper dut_;
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
