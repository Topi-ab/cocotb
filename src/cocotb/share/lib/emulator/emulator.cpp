// emulator.cpp
//
// Minimal cocotb "simulator" runner using GPI (cocotb private C++ API).
// v1 goals:
//   - strict 1 ps steps timebase (uint64_t)
//   - timed callbacks (Timer) supported via event queue
//   - RW/RO/NextTime callbacks supported as phase hooks
//   - blind put() accepted; get() returns last written (or 0)
//
// Not supported in v1: edge triggers, hierarchy iteration, 4-state values, HW tick.
//
// Build: you already have include paths correct for gpi.h and gpi_priv.h.

#include <stdio.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <dlfcn.h>

#include "emulator.hpp"
#include "gpi.h"
#include "gpi_priv.hpp"

// ----------------------- Timebase -----------------------
using sim_time_t = std::uint64_t;     // 1 step = 1 ps
static constexpr int32_t SIM_PRECISION_LOG10 = -12;  // 1 ps

// ----------------------- Event Queue (Design A) -----------------------
struct CancelToken {
    bool cancelled = false;
};
using CancelTokenPtr = std::shared_ptr<CancelToken>;
using Action = std::function<void()>;

struct Event {
    Action action;
    CancelTokenPtr token;
    std::uint64_t seq = 0;
};

class EventQueue {
public:
    CancelTokenPtr schedule_at(sim_time_t t, Action action) {
        auto token = std::make_shared<CancelToken>();
        timed_[t].push_back(Event{std::move(action), token, next_seq_++});
        return token;
    }

    CancelTokenPtr schedule_now(Action action) {
        auto token = std::make_shared<CancelToken>();
        immediate_.push_back(Event{std::move(action), token, next_seq_++});
        return token;
    }

    static void cancel(const CancelTokenPtr &tok) {
        if (tok) tok->cancelled = true;
    }

    bool has_timed() const { return !timed_.empty(); }
    bool has_immediate() const { return !immediate_.empty(); }
    bool empty() const { return timed_.empty() && immediate_.empty(); }

    std::size_t run_immediate_all() {
        std::size_t executed = 0;
        while (!immediate_.empty()) {
            Event ev = std::move(immediate_.front());
            immediate_.pop_front();
            if (ev.token && ev.token->cancelled) continue;
            ev.action();
            executed++;
        }
        return executed;
    }

    std::optional<sim_time_t> promote_earliest_bucket_to_immediate(sim_time_t now) {
        if (timed_.empty()) return std::nullopt;
        auto it = timed_.begin();
        sim_time_t t_next = it->first;
        if (t_next < now) {
            throw std::runtime_error("EventQueue: time went backwards (bug)");
        }
        auto &bucket = it->second;
        while (!bucket.empty()) {
            immediate_.push_back(std::move(bucket.front()));
            bucket.pop_front();
        }
        timed_.erase(it);
        return t_next;
    }

private:
    std::deque<Event> immediate_;
    std::map<sim_time_t, std::deque<Event>> timed_;
    std::uint64_t next_seq_ = 0;
};

// ----------------------- Signal storage -----------------------
struct SignalState {
    std::string last_bin = "0";
    int32_t last_i32 = 0;
    int32_t width = 1;
};

class EmuContext;

// ----------------------- Minimal handles -----------------------
class EmuObj : public GpiObjHdl {
public:
    EmuObj(GpiImplInterface *impl, EmuContext *ctx, void *raw, gpi_objtype t, bool is_const)
        : GpiObjHdl(impl, raw, t, is_const), ctx_(ctx) {}

    EmuContext *ctx() const { return ctx_; }

private:
    EmuContext *ctx_;
};

class EmuValueChangeCb;  // fwd
enum class ValueEdge;

// ----------------------- Adapter API -----------------------

class EmuSignal : public GpiSignalObjHdl {
public:
    EmuSignal(GpiImplInterface *impl,
              EmuContext *ctx,
              AdapterApi *adapter,
              const std::string &name,
              const std::string &fullname,
              SignalState *st,
              void *adapter_hdl)
        : GpiSignalObjHdl(impl, adapter_hdl, (st && st->width == 1) ? GPI_LOGIC : GPI_LOGIC_ARRAY, false),
          ctx_(ctx),
          adapter_(adapter),
          state_(st),
          adapter_hdl_(adapter_hdl) {
        m_name = name;
        m_fullname = fullname;
        m_length = state_ ? state_->width : 1;
        m_const = false;
    }

    // Readbacks
    const char *get_signal_value_binstr() override {
        refresh_from_adapter();
        tmp_ = state_ ? state_->last_bin : std::string("0");
        return tmp_.c_str();
    }

    const char *get_signal_value_str() override {
        refresh_from_adapter();
        tmp_ = state_ ? state_->last_bin : std::string("0");
        return tmp_.c_str();
    }

    double get_signal_value_real() override { return 0.0; }

    long get_signal_value_long() override {
        refresh_from_adapter();
        // cocotb API still exposes long reads; we return the stored int32.
        return state_ ? static_cast<long>(state_->last_i32) : 0L;
    }

    // Writes (blind put)
    int set_signal_value(const int32_t value, gpi_set_action action) override {
        (void)action;
        if (!state_) return -1;
        int32_t old = state_->last_i32;
        state_->last_i32 = value;
        // v1: simplistic bin conversion for 0/1 only (expand later)
        if (adapter_ && adapter_->set_i32) {
            if (adapter_->set_i32(adapter_->obj, adapter_hdl_, value) != 0) {
                return -1;
            }
        }
        state_->last_bin = (value == 0) ? "0" : "1";
        notify_value_change(old, state_->last_i32);
        return 0;
    }

    int set_signal_value(const double /*value*/, gpi_set_action /*action*/) override {
        // v1: reject real
        return -1;
    }

    int set_signal_value_str(std::string &value, gpi_set_action action) override {
        (void)action;
        if (!state_) return -1;
        int32_t old = state_->last_i32;
        state_->last_bin = value;
        if (value == "0") state_->last_i32 = 0;
        else if (value == "1") state_->last_i32 = 1;
        if (adapter_ && adapter_->set_i32) {
            if (adapter_->set_i32(adapter_->obj, adapter_hdl_, state_->last_i32) != 0) {
                return -1;
            }
        }
        notify_value_change(old, state_->last_i32);
        return 0;
    }

    int set_signal_value_binstr(std::string &value, gpi_set_action action) override {
        return set_signal_value_str(value, action);
    }

    // Value change callbacks (basic rising/falling/value change)
    GpiCbHdl *register_value_change_callback(
        gpi_edge edge, int (*gpi_function)(void *), void *cb_data) override;

    void notify_value_change(int32_t old_val, int32_t new_val);

private:
    EmuContext *ctx_;
    AdapterApi *adapter_;
    SignalState *state_;
    void *adapter_hdl_;
    std::string tmp_;
    std::vector<EmuValueChangeCb *> value_cbs_;

    void refresh_from_adapter() {
        if (!state_ || !adapter_ || !adapter_->get_i32) return;
        int32_t val = state_->last_i32;
        if (adapter_->get_i32(adapter_->obj, adapter_hdl_, &val) != 0) return;
        if (val != state_->last_i32) {
            int32_t old = state_->last_i32;
            state_->last_i32 = val;
            state_->last_bin = (val == 0) ? "0" : "1";
            notify_value_change(old, val);
        } else {
            state_->last_i32 = val;
            state_->last_bin = (val == 0) ? "0" : "1";
        }
    }

public:
    void *adapter_handle() const { return adapter_hdl_; }
};

// ----------------------- Callbacks -----------------------
class EmuCbBase : public GpiCbHdl {
public:
    EmuCbBase(GpiImplInterface *impl, EmuContext *ctx, int (*fn)(void *), void *data)
        : GpiCbHdl(impl), ctx_(ctx) {
        set_cb_info(fn, data);
    }

    EmuContext *ctx() const { return ctx_; }

protected:
    EmuContext *ctx_;
};

enum class PhaseKind { ReadWrite, ReadOnly, NextTime };
enum class ValueEdge { Rising, Falling, Any };

/*class EmuPhaseCb final : public EmuCbBase {
public:
    EmuPhaseCb(GpiImplInterface *impl, EmuContext *ctx, PhaseKind kind, int (*fn)(void *), void *data)
        : EmuCbBase(impl, ctx, fn, data), kind_(kind) {}

    PhaseKind kind() const { return kind_; }

    // GpiCbHdl interface
    int arm() override { return 0; }     // persistent in v1
    int remove() override { return 0; }  // no-op
    int run() override {
        // Calls into cocotb core
        return m_cb_func ? m_cb_func(m_cb_data) : 0;
    }

private:
    PhaseKind kind_;
};*/

class EmuPhaseCb final : public EmuCbBase {
public:
    EmuPhaseCb(GpiImplInterface *impl, EmuContext *ctx, PhaseKind kind,
               int (*fn)(void *), void *data)
        : EmuCbBase(impl, ctx, fn, data), kind_(kind) {}

    PhaseKind kind() const { return kind_; }
    bool active() const { return active_; }

    int arm() override { return 0; }     // persistent
    int remove() override { active_ = false; return 0; }

    int run() override {
        if (!active_) return 0;
        int rc = m_cb_func ? m_cb_func(m_cb_data) : 0;
        active_ = false;
        return rc;
    }

private:
    PhaseKind kind_;
    bool active_ = true;
};

class EmuValueChangeCb final : public EmuCbBase {
public:
    EmuValueChangeCb(GpiImplInterface *impl,
                     EmuContext *ctx,
                     ValueEdge edge,
                     int (*fn)(void *),
                     void *data)
        : EmuCbBase(impl, ctx, fn, data), edge_(edge) {}

    int arm() override { return 0; }
    int remove() override {
        active_ = false;
        return 0;
    }
    int run() override {
        if (!active_) return 0;
        int rc = m_cb_func ? m_cb_func(m_cb_data) : 0;
        active_ = false;
        return rc;
    }

    ValueEdge edge() const { return edge_; }
    bool active() const { return active_; }

private:
    ValueEdge edge_;
    bool active_ = true;
};





/*class EmuTimedCb final : public EmuCbBase {
public:
    EmuTimedCb(GpiImplInterface *impl, EmuContext *ctx, sim_time_t fire_t, int (*fn)(void *), void *data)
        : EmuCbBase(impl, ctx, fn, data), fire_time_(fire_t) {}

    void set_token(CancelTokenPtr tok) { token_ = std::move(tok); }
    const CancelTokenPtr &token() const { return token_; }
    sim_time_t fire_time() const { return fire_time_; }

    // GpiCbHdl interface
    int arm() override { return 0; }  // already scheduled at construction time
    int remove() override {
        EventQueue::cancel(token_);
        return 0;
    }
    int run() override {
        return m_cb_func ? m_cb_func(m_cb_data) : 0;
    }

private:
    sim_time_t fire_time_;
    CancelTokenPtr token_;
};*/



class EmuTimedCb final : public EmuCbBase {
public:
    EmuTimedCb(GpiImplInterface *impl, EmuContext *ctx, sim_time_t fire_t,
               int (*fn)(void *), void *data)
        : EmuCbBase(impl, ctx, fn, data), fire_time_(fire_t) {}

    void set_token(CancelTokenPtr tok) { token_ = std::move(tok); }

    int arm() override { return 0; }

    int remove() override {
        active_ = false;
        EventQueue::cancel(token_);
        return 0;
    }

    int run() override {
        if (!active_) return 0;
        active_ = false;               // critical: one-shot
        EventQueue::cancel(token_);    // belt-and-suspenders
        return m_cb_func ? m_cb_func(m_cb_data) : 0;
    }

private:
    sim_time_t fire_time_;
    CancelTokenPtr token_;
    bool active_ = true;
};





// ----------------------- Emulator context -----------------------
class EmuContext {
public:
    void set_adapter(AdapterApi *adapter) { adapter_ = adapter; }

    sim_time_t now() const { return now_; }
    void set_now(sim_time_t t) { now_ = t; }

    EventQueue &eq() { return eq_; }

    SignalState &ensure_signal(const std::string &fullname) {
        auto it = signals_.find(fullname);
        if (it == signals_.end()) {
            it = signals_.emplace(fullname, SignalState{}).first;
        }
        return it->second;
    }

    void register_signal_handle(const std::string &fullname, EmuSignal *sig) {
        signal_handles_[fullname].push_back(sig);
    }

    int32_t get_signal_i32(const std::string &fullname) {
        printf("EmuContext::get_signal_i32(%s)\n", fullname.c_str());
        auto it = signals_.find(fullname);
        if (it == signals_.end()) return 0;
        auto hit = signal_handles_.find(fullname);
        if (adapter_ && adapter_->get_i32 && hit != signal_handles_.end() && !hit->second.empty()) {
            EmuSignal *sig = hit->second.front();
            int32_t val = it->second.last_i32;
            if (adapter_->get_i32(adapter_->obj, sig->adapter_handle(), &val) == 0) {
                it->second.last_i32 = val;
                it->second.last_bin = (val == 0) ? "0" : "1";
            }
        }
        return it->second.last_i32;
    }

    void set_signal_i32(const std::string &fullname, int32_t value) {
        printf("EmuContext::set_signal_i32(%s, %d)\n", fullname.c_str(), value);
        auto &st = ensure_signal(fullname);
        int32_t old = st.last_i32;
        st.last_i32 = value;
        st.last_bin = (value == 0) ? "0" : "1";
        auto hit = signal_handles_.find(fullname);
        if (adapter_ && adapter_->set_i32 && hit != signal_handles_.end() && !hit->second.empty()) {
            EmuSignal *sig = hit->second.front();
            adapter_->set_i32(adapter_->obj, sig->adapter_handle(), value);
        }
        auto it = signal_handles_.find(fullname);
        if (it != signal_handles_.end()) {
            for (auto *sig : it->second) {
                if (sig) sig->notify_value_change(old, value);
            }
        }
    }

    void add_phase_cb(EmuPhaseCb *cb) { phase_cbs_.push_back(cb); }
    bool has_phase_cbs() const { return !phase_cbs_.empty(); }

    void fire_phase(PhaseKind k) {
        for (std::size_t i = 0; i < phase_cbs_.size();) {
            EmuPhaseCb *cb = phase_cbs_[i];
            if (!cb) {
                phase_cbs_.erase(phase_cbs_.begin() + i);
                continue;
            }
            if (cb->kind() == k) {
                cb->run();
            }
            if (!cb->active()) {
                delete cb;
                phase_cbs_.erase(phase_cbs_.begin() + i);
                continue;
            }
            ++i;
        }
    }

    void request_shutdown(int code) {
        shutdown_requested_ = true;
        exit_code_ = code;
    }

    bool shutdown_requested() const { return shutdown_requested_; }
    int exit_code() const { return exit_code_; }

    void propagate_py_to_cpp() {
        const std::string src = "dut.py_to_cpp";
        const std::string dst = "dut.cpp_to_py";
        int32_t val = get_signal_i32(src);
        int32_t old_val = has_seen_py_to_cpp_ ? last_py_to_cpp_ : -1;
        if (!has_seen_py_to_cpp_ || val != last_py_to_cpp_) {
            int32_t dst_val = (val == 0) ? 1 : 0;
            printf("[propagate] t=%10lu  src=%-10s old=%2d new=%2d  dst=%-10s set=%2d\n",
                   static_cast<unsigned long>(now_),
                   src.c_str(), old_val, val,
                   dst.c_str(), dst_val);
            last_py_to_cpp_ = val;
            has_seen_py_to_cpp_ = true;
            set_signal_i32(dst, dst_val);
        }
    }




    void remove_phase_cb(GpiCbHdl *cb) {
        auto &v = phase_cbs_;
        v.erase(std::remove(v.begin(), v.end(), cb), v.end());
    }





private:
    sim_time_t now_ = 0;
    EventQueue eq_;
    std::unordered_map<std::string, SignalState> signals_;
    std::unordered_map<std::string, std::vector<EmuSignal *>> signal_handles_;
    std::vector<EmuPhaseCb *> phase_cbs_;

    bool shutdown_requested_ = false;
    int exit_code_ = 0;
    bool has_seen_py_to_cpp_ = false;
    int32_t last_py_to_cpp_ = 0;
    AdapterApi *adapter_ = nullptr;
};

// ----------------------- EmuSignal helpers -----------------------
GpiCbHdl *EmuSignal::register_value_change_callback(
    gpi_edge edge, int (*gpi_function)(void *), void *cb_data) {
    printf("[vcb] register %s edge=%d\n", m_fullname.c_str(), static_cast<int>(edge));
    ValueEdge ve = ValueEdge::Any;
    if (edge == GPI_RISING) ve = ValueEdge::Rising;
    else if (edge == GPI_FALLING) ve = ValueEdge::Falling;

    auto *cb = new EmuValueChangeCb(m_impl, ctx_, ve, gpi_function, cb_data);
    value_cbs_.push_back(cb);
    return cb;
}

void EmuSignal::notify_value_change(int32_t old_val, int32_t new_val) {
    if (value_cbs_.empty()) return;
    for (std::size_t i = 0; i < value_cbs_.size();) {
        EmuValueChangeCb *cb = value_cbs_[i];
        if (!cb || !cb->active()) {
            value_cbs_.erase(value_cbs_.begin() + i);
            continue;
        }

        bool fire = false;
        switch (cb->edge()) {
            case ValueEdge::Any:
                fire = (old_val != new_val);
                break;
            case ValueEdge::Rising:
                fire = (old_val == 0 && new_val != 0);
                break;
            case ValueEdge::Falling:
                fire = (old_val != 0 && new_val == 0);
                break;
        }

        if (fire) {
            printf("[vcb] fire %s old=%d new=%d edge=%d\n",
                   m_fullname.c_str(), old_val, new_val, static_cast<int>(cb->edge()));
            EmuValueChangeCb *cb_to_run = cb;
            ctx_->eq().schedule_now([cb_to_run]() {
                cb_to_run->run();
                delete cb_to_run;
            });
            value_cbs_.erase(value_cbs_.begin() + i);
            continue;
        }
        ++i;
    }
}

// ----------------------- GPI implementation -----------------------
class EmuImpl final : public GpiImplInterface {
public:
    explicit EmuImpl(EmuContext &ctx, AdapterApi &adapter)
        : GpiImplInterface("cocotb-emulator-v1"), ctx_(ctx), adapter_(adapter) {
        if (!adapter_.catalog || !adapter_.catalog->signals || adapter_.catalog->count <= 0) {
            throw std::runtime_error("adapter catalog is missing or empty");
        }
        ctx_.set_adapter(&adapter_);

        for (int i = 0; i < adapter_.catalog->count; ++i) {
            const EmuAdapterSignal &sig = adapter_.catalog->signals[i];
            if (!sig.fullname && !sig.name) continue;
            std::string fullname = sig.fullname ? sig.fullname : sig.name;
            std::string name = sig.name ? sig.name : fullname;

            auto &st = ctx_.ensure_signal(fullname);
            st.width = sig.width > 0 ? sig.width : 1;

            if (sig.is_root) {
                if (!root_) {
                    auto obj = std::make_unique<EmuObj>(this, &ctx_, sig.handle, GPI_MODULE, true);
                    obj->initialise(name, fullname);
                    root_ = std::move(obj);
                }
                continue;
            }

            auto hdl = std::make_unique<EmuSignal>(this, &ctx_, &adapter_, name, fullname, &st, sig.handle);
            auto *raw = hdl.get();
            handle_cache_.emplace(fullname, std::move(hdl));
            ctx_.register_signal_handle(fullname, raw);
        }

        if (!root_) {
            throw std::runtime_error("adapter catalog provided no root object");
        }
    }

    // Required by your header: precision is returned via pointer
    void get_sim_precision(int32_t *precision) override {
        *precision = SIM_PRECISION_LOG10;
    }

    // Current time in steps (1 ps)
    void get_sim_time(uint32_t *high, uint32_t *low) override {
        sim_time_t t = ctx_.now();
        *low = static_cast<uint32_t>(t & 0xFFFFFFFFu);
        *high = static_cast<uint32_t>((t >> 32) & 0xFFFFFFFFu);
    }

    const char *get_simulator_product() override { return "cocotb-emulator-v1"; }
    const char *get_simulator_version() override { return "0.1"; }

    const char *reason_to_string(int reason) {
        // v1: minimal; cocotb mostly uses this for logging.
        switch (reason) {
            default: return "UNKNOWN";
        }
    }

    // Root handle
    GpiObjHdl *get_root_handle(const char *name) override {
        (void)name;
        if (!root_) return nullptr;
        return root_.get();
    }

    // Child lookup (API names per your gpi_priv.h)
    GpiObjHdl *get_child_by_name(const std::string &name, GpiObjHdl *parent) override {
        if (!parent) return nullptr;
        std::string full = parent->get_fullname();
        if (!full.empty()) full += ".";
        full += name;

        auto it = handle_cache_.find(full);
        if (it != handle_cache_.end()) return it->second.get();
        return nullptr;
    }

    GpiObjHdl *get_child_by_index(int32_t /*index*/, GpiObjHdl * /*parent*/) override {
        return nullptr;
    }

    GpiObjHdl *get_child_from_handle(void * /*raw_hdl*/, GpiObjHdl * /*parent*/) override {
        return nullptr;
    }

    GpiIterator *iterate_handle(GpiObjHdl * /*obj_hdl*/, gpi_iterator_sel /*sel*/) override {
        return nullptr;
    }

    // Callbacks use signature int(*)(void*), void*
    GpiCbHdl *register_timed_callback(uint64_t time, int (*gpi_function)(void *), void *cb_data) override {
        // time is in simulator steps (1 ps)
        sim_time_t fire_t = ctx_.now() + static_cast<sim_time_t>(time);
        auto *hdl = new EmuTimedCb(this, &ctx_, fire_t, gpi_function, cb_data);

        auto token = ctx_.eq().schedule_at(fire_t, [hdl]() {
            // Execute if not cancelled (cancellation handled by EventQueue token + skip)
            hdl->run();
        });
        hdl->set_token(token);
        return hdl;
    }

    GpiCbHdl *register_readonly_callback(int (*gpi_function)(void *), void *cb_data) override {
        auto *hdl = new EmuPhaseCb(this, &ctx_, PhaseKind::ReadOnly, gpi_function, cb_data);
        ctx_.add_phase_cb(hdl);
        return hdl;
    }

    GpiCbHdl *register_nexttime_callback(int (*gpi_function)(void *), void *cb_data) override {
        auto *hdl = new EmuPhaseCb(this, &ctx_, PhaseKind::NextTime, gpi_function, cb_data);
        ctx_.add_phase_cb(hdl);
        return hdl;
    }

    GpiCbHdl *register_readwrite_callback(int (*gpi_function)(void *), void *cb_data) override {
        auto *hdl = new EmuPhaseCb(this, &ctx_, PhaseKind::ReadWrite, gpi_function, cb_data);
        ctx_.add_phase_cb(hdl);
        return hdl;
    }


/*int deregister_callback(GpiCbHdl *cb_hdl) override {
    if (!cb_hdl) return 0;
    cb_hdl->remove();
    // Optional but good: erase from phase list if present
    ctx_.remove_phase_cb(cb_hdl);   // implement as erase-remove
    delete cb_hdl;
    return 0;
}*/





    void sim_end() override {
        ctx_.request_shutdown(0);
    }

private:
    EmuContext &ctx_;
    AdapterApi &adapter_;
    std::unique_ptr<EmuObj> root_;
    std::unordered_map<std::string, std::unique_ptr<GpiObjHdl>> handle_cache_;
};

static AdapterApi load_adapter() {
    const char *path = std::getenv("EMULATOR_ADAPTER_SO");
    if (!path || !*path) {
        path = "libemu_adapter.so";
    }

    AdapterApi api{};
    api.handle = dlopen(path, RTLD_NOW);
    if (!api.handle) {
        std::cerr << "ERROR: unable to load adapter " << path << ": " << dlerror()
                  << "\n";
        return api;
    }
    dlerror();  // clear
    api.create = reinterpret_cast<void *(*)()>(dlsym(api.handle, "emulator_adapter_create"));
    api.destroy = reinterpret_cast<void (*)(void *)>(dlsym(api.handle, "emulator_adapter_destroy"));
    api.name_obj = reinterpret_cast<const char *(*)(void *)>(dlsym(api.handle, "emulator_adapter_name_obj"));
    api.init_obj = reinterpret_cast<int (*)(void *)>(dlsym(api.handle, "emulator_adapter_init_obj"));
    api.shutdown_obj = reinterpret_cast<int (*)(void *)>(dlsym(api.handle, "emulator_adapter_shutdown_obj"));
    api.catalog_fn = reinterpret_cast<const EmuAdapterCatalog *(*)(void *)>(
        dlsym(api.handle, "emulator_adapter_catalog"));
    api.set_i32 = reinterpret_cast<int (*)(void *, void *, int32_t)>(
        dlsym(api.handle, "emulator_adapter_set_i32"));
    api.get_i32 = reinterpret_cast<int (*)(void *, void *, int32_t *)>(
        dlsym(api.handle, "emulator_adapter_get_i32"));
    if (!api.create || !api.destroy || !api.name_obj || !api.init_obj || !api.shutdown_obj || !api.catalog_fn ||
        !api.set_i32 || !api.get_i32) {
        std::cerr << "ERROR: adapter missing required symbols\n";
        dlclose(api.handle);
        api.handle = nullptr;
    }
    return api;
}

static void unload_adapter(AdapterApi &api) {
    if (api.shutdown_obj && api.obj) api.shutdown_obj(api.obj);
    if (api.destroy && api.obj) api.destroy(api.obj);
    if (api.handle) dlclose(api.handle);
    api = AdapterApi{};
}

static bool ensure_env_set(const char *name) {
    const char *val = std::getenv(name);
    if (!val || *val == '\0') {
        std::cerr << "ERROR: environment variable " << name << " is required\n";
        return false;
    }
    return true;
}

// ----------------------- main -----------------------
int main(int argc, char **argv) {
    try {
        const char *required_env[] = {"PYGPI_PYTHON_BIN", "COCOTB_TEST_MODULES"};
        for (const char *name : required_env) {
            if (!ensure_env_set(name)) {
                return 2;
            }
        }

        AdapterApi adapter = load_adapter();
        if (!adapter.handle) {
            return 2;
        }
        adapter.obj = adapter.create ? adapter.create() : nullptr;
        if (!adapter.obj) {
            std::cerr << "ERROR: adapter create failed\n";
            unload_adapter(adapter);
            return 2;
        }
        if (adapter.init_obj && adapter.init_obj(adapter.obj) != 0) {
            std::cerr << "ERROR: adapter init failed\n";
            unload_adapter(adapter);
            return 2;
        }
        adapter.catalog = adapter.catalog_fn ? adapter.catalog_fn(adapter.obj) : nullptr;
        if (!adapter.catalog) {
            std::cerr << "ERROR: adapter catalog missing\n";
            unload_adapter(adapter);
            return 2;
        }
        std::cout << "[emulator] using adapter: "
                  << (adapter.name_obj ? adapter.name_obj(adapter.obj) : "unknown") << "\n";

        EmuContext ctx;
        EmuImpl impl(ctx, adapter);

        if (gpi_register_impl(&impl) != 0) {
            std::cerr << "ERROR: gpi_register_impl failed\n";
            return 2;
        }

        gpi_init_logging_and_debug();
        gpi_entry_point();              // loads PyGPI entrypoint
        gpi_start_of_sim_time(argc, argv);  // kick off cocotb

        // Minimal phase loop: { RW ; RO ; NT }*
        while (!ctx.shutdown_requested()) {
            // Drain delta/immediate work
            while (ctx.eq().run_immediate_all() > 0) {}

            // RW at time T
            ctx.fire_phase(PhaseKind::ReadWrite);
            if (ctx.shutdown_requested()) break;

            // RO at time T
            ctx.fire_phase(PhaseKind::ReadOnly);
            if (ctx.shutdown_requested()) break;

            // Drain immediate work generated by RW/RO
            while (ctx.eq().run_immediate_all() > 0) {}

            // Propagate after RO (at current time)
            ctx.propagate_py_to_cpp();

            // NT hook
            ctx.fire_phase(PhaseKind::NextTime);
            if (ctx.shutdown_requested()) break;

            // Drain any immediate work generated by propagation/NT
            while (ctx.eq().run_immediate_all() > 0) {}

            // Advance time to next timed bucket, if any
            if (!ctx.eq().has_timed()) {
                if (!ctx.has_phase_cbs()) break;
                continue;
            }
            auto t_next = ctx.eq().promote_earliest_bucket_to_immediate(ctx.now());
            if (!t_next) break;
            ctx.set_now(*t_next);

            // Execute all timed callbacks scheduled for new time
            ctx.eq().run_immediate_all();
        }

        // End embedded python
        gpi_end_of_sim_time();
        unload_adapter(adapter);
        return ctx.exit_code();
    } catch (const std::exception &e) {
        std::cerr << "FATAL: " << e.what() << "\n";
        return 99;
    }
}
