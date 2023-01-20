/** @file debug.h */

#pragma once
#ifndef SRC_COCLASSES_SRC_COCLASSES_DEBUG_H_
#define SRC_COCLASSES_SRC_COCLASSES_DEBUG_H_
#include <exception>
#include <string>
#include <vector>



#ifndef NDEBUG
#ifdef _WIN32
#include <debugapi.h>
#else
#include <unistd.h>
#include <cxxabi.h>
#endif
#endif

#ifndef NDEBUG
#ifndef COCLS_DEFINE_SET_CORO_NAME
#define COCLS_DEFINE_SET_CORO_NAME
#endif
#endif

#ifdef COCLS_DEFINE_SET_CORO_NAME
#include <unordered_map>
#endif



namespace cocls {


///Reports unhandled exceptions in tasks.
/**
 * If the exception is not retrieves, debug reporter is called and prints the exception at 
 * console or into std error. This is enabled in debug mode. In release mode, no output is
 * genrated. However, you can write own reporter, and register it as global object, which will
 * report such situation in both debug and release mode
 */
class debug_reporter {
public:
    
    virtual ~debug_reporter() = default;

#ifndef NDEBUG
    static void demangle_type(const std::type_info &type, std::string &msg) {
#ifdef _WIN32
        msg.append(type.name());
#else
        int status = -1;
        char *demangled_name = abi::__cxa_demangle(type.name(), NULL, NULL, &status);
        msg.append(demangled_name);
        free(demangled_name);
#endif
    }

    static void output_debug(std::string &&msg) {
#ifdef _WIN32
        OutputDebugStringA(msg.c_str());        
        msg.append("\r\n");
#else
        msg.append("\n");
        ::write(2, msg.data(), msg.size());
#endif
    }

#endif

    virtual void report_exception(std::exception_ptr ptr, const std::type_info &task_type) noexcept {
#ifndef NDEBUG
        std::string msg;
        msg.append("cocls: unhandled exception in a coroutine: ");
        demangle_type(task_type, msg);
        msg.append(" - ");
        try {
            std::rethrow_exception(ptr);
        } catch (std::exception &e) {
            msg.append("what: ");
            msg.append(e.what());
        } catch (...) {
            msg.append("<non-standard exception>");            
        }
        output_debug(std::move(msg));
#endif
    }

    static debug_reporter inst;
    static debug_reporter *current_instance;

#ifdef COCLS_DEFINE_SET_CORO_NAME
    struct CoroInfo {
        std::string name;
        const char *loc;
        const char *fn;
    };
    
    virtual void set_coro_name(std::coroutine_handle<> h, const char *loc, const char *fn, std::string name = std::string()) {
        std::lock_guard _(_name_map_lock);
        _name_map[h] = CoroInfo {
            std::move(name), loc, fn
        };
        coro_monitor_event();
    }
    virtual void coro_destroyed(std::coroutine_handle<> h) noexcept {
        std::lock_guard _(_name_map_lock);
        _name_map.erase(h);
        coro_monitor_event();
    }
    auto get_running_coros() const {
        std::lock_guard _(_name_map_lock);
        return _name_map;
    }
    
    void coro_monitor_register() {
        _flag.clear();
    }
    void coro_monitor_wait() {
        _flag.wait(false);
    }
    void coro_monitor_event() {
        if (!_flag.test_and_set()) {
            _flag.notify_all();
        }
    }
    
#endif     
    
protected:
#ifdef COCLS_DEFINE_SET_CORO_NAME
    mutable std::mutex _name_map_lock;
    std::atomic_flag _flag;
    struct HandleHash {std::size_t operator()(std::coroutine_handle<> h) const {
       return reinterpret_cast<std::size_t>(h.address()); 
    }};
    std::unordered_map<std::coroutine_handle<>, CoroInfo, HandleHash> _name_map;    
#endif
    
};

inline debug_reporter debug_reporter::inst;
inline debug_reporter *debug_reporter::current_instance = &inst;
}

#ifdef COCLS_DEFINE_SET_CORO_NAME

namespace cocls {

inline void coro_monitor_register() {
    debug_reporter::current_instance->coro_monitor_register();    
}
inline void coro_monitor_wait() {
    debug_reporter::current_instance->coro_monitor_wait();    
}
inline void coro_monitor_event() {
    debug_reporter::current_instance->coro_monitor_event();    
}

class set_coro_name { // @suppress("Miss copy constructor or assignment operator")
public:
    static bool await_ready() noexcept {return false;}
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        debug_reporter::current_instance->set_coro_name(h, loc, fun, std::move(desc));
        return false;
    }
    static void await_resume() noexcept {};
    
    set_coro_name(const char *loc, const char *fun, std::string desc = std::string())
        :loc(loc),fun(fun),desc(std::move(desc)) {}
    
protected:
    const char *loc;
    const char *fun;
    std::string desc;
    std::coroutine_handle<> _h;
};


#define COCLS_SET_CORO_NAME(...) co_await ::cocls::set_coro_name(__FILE__, __FUNCTION__ __VA_OPT__(,) __VA_ARGS__)


}
#else 
#define COCLS_SET_CORO_NAME(...)
#endif



#endif /* SRC_COCLASSES_SRC_COCLASSES_DEBUG_H_ */

