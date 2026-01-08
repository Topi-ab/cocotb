#include <cstdio>
#include <string>

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

AdapterStub &instance() {
    static AdapterStub inst;
    return inst;
}

}  // namespace

extern "C" {

const char *emulator_adapter_name() {
    return instance().name();
}

int emulator_adapter_init() {
    return instance().init();
}

int emulator_adapter_shutdown() {
    return instance().shutdown();
}

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

}  // extern "C"
