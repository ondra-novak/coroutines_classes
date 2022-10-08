#pragma once
#ifndef SRC_COCLASSES_NOCORO_H_
#define SRC_COCLASSES_NOCORO_H_

#include "handle.h"

#include "resume_lock.h"

#include "sync_await.h"
#include <optional>
#include <variant>
namespace cocls {


template<typename T>
class nocoro_awaiter;

///Simulates coroutine - you can use this in non-coroutine code
/**
 * Because coroutines are always dynamically allocated, sometimes, for very 
 * short coroutines, it is less effecient to use coroutine at all. Instead, you
 * can declare a class, which inherits nocoro and can be used instead coroutine
 * 
 * @tparam Ret return value of simulated coroutine
 * @tparam Awaiters list of awaiters, used during executions - because space
 * for awaiters must be preallocated inside of the object
 * 
 * @note To use this class, all awaiters must be move constructible and must
 * support handle_t.
 * 
 * The coroutine code can be started by function start(). It executes code
 * on_run();
 * 
 * Inside of code, you can use function await()
 * 
 * @code
 * await(this, &T::resume_point, <awaitable object>)
 * @endcode
 * 
 * You need to declare function which acts as resume point. When simulated
 * coroutune is resumed, the resume_point is called. Then, function must 
 * call get_result<type>() to obtain result of await()
 * 
 * @code
 * void SimCoro::on_run() {
 *      await(this, &SimCoro::goon, stream.read());
 * }
 * 
 * void SimCoro::goon() {
 *      std::string_view data = get_result<std::string_view>();
 *      _return(data);
 * }
 * @endcode
 * 
 * 
 * Simulated coroutine needs to call _return(<value>). If the coroutine doesn't
 * return value, it needs to call _return(). Without this call, coroutine
 * will seen as still running.
 * 
 * 
 * Caller can co_await or sync_await on the instance of simulated coroutine
 * 
 */
template<typename Ret, typename ... Awaiters>
class nocoro: public abstract_resumable_t {
public:
    
    virtual void on_run() = 0;
    virtual void on_unhandled_exception() {
        _return.on_exception();
    }
    
    class awaiter {
        nocoro_awaiter<Ret> &awt;
    public:
        awaiter (nocoro_awaiter<Ret> &awt):awt(awt) {}
        auto await_ready() {return awt.await_ready();}
        auto await_suspend(handle_t h) {return awt.await_suspend(h);}
        auto await_resume() {return awt.await_resume();}
    };
    
    awaiter operator co_await() {
        return awaiter(_return);
    }
    
    auto join() {
        sync_await(operator co_await());
    }
    
    void start() {
        try {
            on_run();
        } catch (...) {
            on_unhandled_exception();
        }
    }
    
protected:
    class empty {
    public:
        static empty await_resume() {return empty();}
        bool await_ready() {return true;}
        std::coroutine_handle<> await_suspend(handle_t) {
            return std::noop_coroutine();
        }
    };
    
    
    template<typename T>
    T get_result() {
        std::optional<T> out;
        std::visit([&](auto &&arg){
            if constexpr (std::is_convertible_v<decltype(arg.await_resume()), T>) {
                out = std::move(arg.await_resume());
            } else {
                throw std::bad_cast();
            }
        },_awt);
        return *out;
    }

    template<>
    void get_result() {        
        std::visit([&](auto &&arg){
            if constexpr (std::is_void<decltype(arg.await_resume())>::value) {
                arg.await_resume();
            } else {
                throw std::bad_cast();
            }
        },_awt);
    }

    
    template<typename A>
    auto await(A &&a) -> decltype(a.await_ready()) {
        _awt.template emplace<A>(std::forward<A>(a));
        bool ready = std::visit([](auto &&arg){
            return arg.await_ready();
        }, _awt);
        if (ready) {
            resume_lock::resume(this);
        } else {
            resume_lock::resume(std::visit([this](auto &&arg){
                return arg.await_suspend(handle_t(this));
            }, _awt));
        }
        return ready;
    }

    template<typename A>
    auto await(A &&a) -> decltype(a.operator co_await().await_ready()) {
        return await(a.operator co_await());
    }

    template<typename T, typename A>
    void await(T *me, void (T::*resume_point)(), A &&awaiter) {
        set_resume_point(me, resume_point);
        await(std::forward<A>(awaiter));
    }
    
    virtual std::coroutine_handle<> resume() noexcept {
        try {
            _resume_point(this);            
        } catch (...) {
            on_unhandled_exception();
        }
        if (_resume_return) {
            auto x = _resume_return;
            _resume_return= nullptr;
            return x;
        } else {
            return std::noop_coroutine();
        }
    }
    
    template<typename T, void (T::*resume_ptr)()>
    static void resume_point_impl(nocoro *ctx) {
        nocoro *x = static_cast<T *>(ctx);
        (x->*resume_ptr)();
    }
    
    
    using ResumePointFn = void (*)(void *ctx);
    
    template<typename T>
    static void do_resume(void *x) {
        T *ptr = reinterpret_cast<T *>(x);
        using ResumeFn = void (T::*)();
        ResumeFn *fnptr = reinterpret_cast<ResumeFn *>(ptr->_resume_point_storage.data());
        ((*ptr).*(*fnptr))();                
    }

    template<typename T>   
    void set_resume_point(T *ptr, void (T::*fn)()) {
        _resume_point_storage.resize(sizeof(fn));
        using ResumeFn = void (T::*)();
        new(_resume_point_storage.data()) ResumeFn(fn);
        _resume_point = &do_resume<T>;
    }
    
    std::variant<empty, Awaiters...> _awt;
    std::coroutine_handle<> _resume_return;
    nocoro_awaiter<Ret> _return;
    ResumePointFn _resume_point = nullptr;
    std::string _resume_point_storage;
    

};


template<typename T>
class nocoro_awaiter {
    std::atomic<bool> _ready = false;
    std::variant<std::exception_ptr, T> _value;
    handle_t _waiting;
public:
    bool await_ready() {
        return _ready;        
    }
    std::coroutine_handle<> await_suspend(handle_t h) {
        _waiting = h;
        if (_ready) return h.resume_handle();
        return resume_lock::await_suspend();
    }
    auto await_resume() {
       if (_value.index() == 0) {
           std::rethrow_exception(std::get<0>(_value));
       } else {
           return std::get<1>(_value);
       }
    }
    void operator()(T &&x) {
        _value.emplace(std::forward<T>(x));
        _ready = true;
        if (_waiting) resume_lock::resume(_waiting);
    }
    void on_exception() {
        _value.emplace(std::current_exception());
        _ready = true;
        if (_waiting) resume_lock::resume(_waiting);
    }
};

template<>
class nocoro_awaiter<void> {
    std::atomic<bool> _ready = false;
    std::exception_ptr  _exp;
    handle_t _waiting;
public:
    bool await_ready() {
        return _ready;        
    }
    std::coroutine_handle<> await_suspend(handle_t h) {
        _waiting = h;
        if (_ready) return h.resume_handle();
        return resume_lock::await_suspend();
    }
    void await_resume() {
       if (_exp != nullptr) {
           std::rethrow_exception(_exp);
       } 
    }
    void operator()() {
        _ready = true;
        if (_waiting) resume_lock::resume(_waiting);
    }
    void on_exception() {
        _exp = std::current_exception();
        _ready = true;
        if (_waiting) resume_lock::resume(_waiting);
    }
};

}




#endif /* SRC_COCLASSES_NOCORO_H_ */
