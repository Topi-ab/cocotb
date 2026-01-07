#include <cstdio>

extern "C" {

const char *emulator_adapter_name() {
    return "adapter_stub";
}

int emulator_adapter_init() {
    std::puts("[adapter_stub] init");
    return 0;
}

int emulator_adapter_shutdown() {
    std::puts("[adapter_stub] shutdown");
    return 0;
}

}  // extern "C"
