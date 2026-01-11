#include <cstdint>
#include <cstdio>
#include <string>

#include "emulator.hpp"

namespace {

class AdapterStub {
public:
    const char *name() const { return name_.c_str(); }

    int init() {
        std::puts("[adapter_stub] init");
        return 0;
    }

    int shutdown() {
        std::puts("[adapter_stub] shutdown");
        return 0;
    }

private:
    std::string name_ = "adapter_stub";
};

// Dummy opaque handles
struct DummyHandle {
    int id;
    int32_t value;
};

DummyHandle root_handle{0, 0};
DummyHandle py_to_cpp_handle{1, 0};
DummyHandle cpp_to_py_handle{2, 0};

EmuAdapterSignal signals[] = {
    {"dut", "dut", 0, &root_handle, -1, true},
    {"py_to_cpp", "dut.py_to_cpp", 1, &py_to_cpp_handle, 0, false},
    {"cpp_to_py", "dut.cpp_to_py", 1, &cpp_to_py_handle, 0, false},
};

const EmuAdapterCatalog catalog{signals, static_cast<int>(sizeof(signals) / sizeof(signals[0]))};

}  // namespace

extern "C" {

void *emulator_adapter_create() {
    return new AdapterStub();
}

void emulator_adapter_destroy(void *hdl) {
    delete static_cast<AdapterStub *>(hdl);
}

const char *emulator_adapter_name_obj(void *hdl) {
    return static_cast<AdapterStub *>(hdl)->name();
}

int emulator_adapter_init_obj(void *hdl) {
    return static_cast<AdapterStub *>(hdl)->init();
}

int emulator_adapter_shutdown_obj(void *hdl) {
    return static_cast<AdapterStub *>(hdl)->shutdown();
}

const EmuAdapterCatalog *emulator_adapter_catalog(void * /*hdl*/) {
    return &catalog;
}

int emulator_adapter_set_i32(void * /*hdl*/, void *sig_handle, int32_t value) {
    auto *h = static_cast<DummyHandle *>(sig_handle);
    std::printf("[adapter_stub] set handle=%d value=%d\n", h ? h->id : -1, value);
    // store value in the handle for later reads
    if (h) h->value = value;
    return 0;
}

int emulator_adapter_get_i32(void * /*hdl*/, void *sig_handle, int32_t *out) {
    auto *h = static_cast<DummyHandle *>(sig_handle);
    int32_t val = h ? h->value : 0;
    std::printf("[adapter_stub] get handle=%d -> %d\n", h ? h->id : -1, val);
    if (out) *out = val;
    return 0;
}

}  // extern "C"
