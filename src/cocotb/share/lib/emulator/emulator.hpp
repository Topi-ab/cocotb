#pragma once

#include <cstdint>

struct EmuAdapterSignal {
    const char *name;
    const char *fullname;
    int32_t width;
    void *handle;
    int parent;    // index into catalog array; -1 for roots
    bool is_root;  // true for top-level objects
};

struct EmuAdapterCatalog {
    const EmuAdapterSignal *signals;
    int count;
};

struct AdapterApi {
    void *handle = nullptr;
    void *obj = nullptr;
    void *(*create)() = nullptr;
    void (*destroy)(void *) = nullptr;
    const char *(*name_obj)(void *) = nullptr;
    int (*init_obj)(void *) = nullptr;
    int (*shutdown_obj)(void *) = nullptr;
    const EmuAdapterCatalog *(*catalog_fn)(void *) = nullptr;
    const EmuAdapterCatalog *catalog = nullptr;
    int (*set_i32)(void *, void *, int32_t) = nullptr;
    int (*get_i32)(void *, void *, int32_t *) = nullptr;
};
