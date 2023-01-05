/**
 * @file future.h
 */
#pragma once
#ifndef SRC_COCLASSES_FUTURE_H_
#define SRC_COCLASSES_FUTURE_H_

#include "awaiter.h"
#include "common.h"
#include "exceptions.h"
#include "future_var.h"



#include "poolalloc.h"
#include "resumption_policy.h"

#include <assert.h>
#include <memory>
#include <mutex>
#include <condition_variable>



namespace cocls {

/*
 *     │                                │
 *     │owner──────────┐                │owner ──┐
 *     │               ▼                │        │
 *     │           ┌────────┐           ▼        │
 *     ▼           │        │      ┌─────────┐   │
 * co_await ◄──────┤ Future │◄─────┤ Promise │◄──┘
 *     │           │        │      └────┬────┘
 *     │           └────────┘           │
 *     │                                │
 *     │                                │
 *     ▼                                ▼
 */

///Future variable
/** Future variable is variable, which receives value in a future.
 * 
 *  This class implements awaitable future variable. You can co_await on the variable,
 *  the coroutine is resumed, once the variable is set. 
 *  
 *  To se the variable, you need to retrieve a promise object as first. There can
 *  be only one promise object. It is movable only. Once the promise object is created,
 *  the future must be resolved before its destruction. The reason for such ordering is
 *  that neither future nor promise performs any kind of allocation. While future
 *  sits in coroutine's frame, the promise is just pointer which can be moved around,
 *  until it is used to set the value, which also invalidates the promise to prevent further
 *  attempts to set value. 
 *  
 *  Neither promise, nor future are MT Safe. Only guaranteed MT safety is for setting
 *  the value, which can be done from different thread, it also assumes, that value
 *  is not read before it is resolved.
 *  
 *  If you need to copyable, MT safe promises which allows to set value independently from
 *  different threads, to achieve for example a race, where first attempt resolves the
 *  future and other addepts are just ignored, you need to use shared_promise
 * 
 */
template<typename T>
class future;
///Promise 
/**
 * Promise is movable only object which serves as pointer to future to be set.
 * Its content is valid until the object is used to set the value, then it becomes
 * invalid. If this object is destroyed without setting the value of the associated
 * future, the future is resolved with the exception "await_canceled_exception"
 */
template<typename T>
class promise;


/*
 *     │                                                       ┌────────────────┐
 *     │owner──────────┐                         ┌─shared_ptr──┤ shared_promise │
 *     │               ▼                         │             └────────────────┘
 *     │           ┌────────┐       allocated    │
 *     ▼           │        │      ┌─────────┐   │             ┌────────────────┐
 * co_await ◄──────┤ future │◄─────┤ promise │◄──┼─shared_ptr──┤ shared_promise │
 *     │           │        │      │         │   │             └────────────────┘
 *     │           └────────┘      └─────────┘   │
 *     │                                         │             ┌────────────────┐
 *     │                                         └─shared_ptr──┤ shared_promise │
 *     ▼                                                       └────────────────┘
 */

///Shared promise
/**
 * Shared promise acts as promise, but allows to be copied. Multiple threads can
 * try to set value, only the first attempt is used, others are ignored.
 * 
 * The shared promise allocates shared space on heap,
 */
template<typename T, typename shared_place = promise<T>>
class shared_promise;
template<typename T, typename shared_place >
class shared_promise_base;
template<typename T>
class promise_base;


template<typename T>
class future_base {
public:
    
    future_base() {}
    future_base(const future_base &) = delete;
    future_base &operator=(const future_base &) = delete;
    
    ///Create a promise
    /**
     * @return a associated promise object. This object can be moved but not copied
     * 
     * @note only one promise can be created. If you need to create copyable object, use
     * get_shared_promise() 
     * 
     */
    promise<T> get_promise() {
        assert(_state == State::init);
        State old = State::init;
        if (_state.compare_exchange_strong(old, State::has_promise, std::memory_order_relaxed)) {
            return promise<T>(*static_cast<future<T> *>(this));
        } else {
            return promise<T>();
        }
    }

    ///Create a shared promise
    /**
     * Creates copyable promise. Only one promise can be created by this way. However you
     * can copy the resulting object many times as you want.
     * 
     * @note This function performs extra stack allocation to hold the shared state
     * 
     * @return copyable opromise
     */
    shared_promise<T> get_shared_promise();

    
    ///Starts waiting for a result
    co_awaiter<future<T> >  operator co_await() {
        return *static_cast<future<T> *>(this);
    }
    
    ///Wait synchronously
    /**
     * @return the value of the future
     */
    decltype(auto) wait() {
        return co_awaiter<future<T> >(*static_cast<future<T> *>(this)).wait();
    }

    
    ///get value
    /**
     * 
     * @return value of the future
     * @exception value_not_ready_exception when value is not ready
     */
    decltype(auto) get() {
        return _value.get();
    }
    ///get value
    /**
     * 
     * @return value of the future
     * @exception value_not_ready_exception when value is not ready
     */
    decltype(auto) get() const {
        return _value.get();
    }

    ~future_base() {
        assert(_state != State::has_promise);
    }

    
protected:

    enum class State {
        init,
        has_promise,
        has_value
    };
    
    std::atomic<State> _state = State::init;
    std::atomic<abstract_awaiter<> *> _awaiter = nullptr;
    future_var<T> _value;    
    
    bool is_ready() {
        return _state.load(std::memory_order_acquire) != State::has_promise;        
    }
    
    bool subscribe_awaiter(abstract_awaiter<> *x) {
        _awaiter.store(x, std::memory_order_relaxed);
        if (is_ready()) {
            auto awt = _awaiter.exchange(nullptr, std::memory_order_relaxed);
            if (awt) return false;
        } 
        return true;
    }
    
    decltype(auto) get_result() {
        return _value.get();
    }
    
    
    friend class promise_base<T>;
    friend class promise<T>;
    friend class abstract_owned_awaiter<future<T> >;
    friend class co_awaiter<future<T> >;
    
    void unhandled_exception() {
        _value.unhandled_exception();
    }      
    void set_value_finish() {
        this->_state.store(State::has_value, std::memory_order_release);
        auto awt = this->_awaiter.exchange(nullptr, std::memory_order_relaxed);
        if (awt) awt->resume();        
    }
};



template<typename T>
class future: public future_base<T> {
public:
protected:
    friend class promise_base<T>;
    friend class promise<T>;

    template<typename X>
    auto set_value(X &&x) -> decltype(std::declval<future_var<T> >().emplace(x)) {
        this->_value.emplace(std::forward<X>(x));
        this->set_value_finish();
    }
};

template<>
class future<void>: public future_base<void> {
public:
protected:
    friend class promise_base<void>;
    friend class promise<void>;

    void set_value() {
        this->_value.emplace();
        this->set_value_finish();
    }
};



template<typename T>
class promise_base {
public:
    promise_base():_owner(nullptr) {}
    explicit promise_base(future<T> &fut):_owner(&fut) {}
    promise_base(const promise_base &other) =delete;
    promise_base(promise_base &&other):_owner(other.claim()) {}
    ~promise_base() {
        release();
    }
    promise_base &operator=(const promise_base &other) = delete;
    promise_base &operator=(promise_base &&other) {
        if (this != &other) {
            if (_owner) release();
            _owner = other.claim();
        }
        return *this;
    }
    
    ///Releases promise without setting a value;
    void release() {
        auto m = claim();
        if (m) {
            try {
                throw await_canceled_exception();
            } catch (...) {
                m->unhandled_exception();
            }
        }
    }



    ///Returns true, if the promise is valid
    operator bool() const {
        return _owner != nullptr;
    }

    ///Returns true, if the promise is not valid
    bool operator !() const {
        return _owner == nullptr;
    }

    ///capture current exception
    void unhandled_exception()  {
        auto m = claim();
        if (m) {
           m->unhandled_exception();
        }
    }

    ///claim this future as pointer to promise - used often internally
    future<T> *claim() {
        return _owner.exchange(nullptr, std::memory_order_relaxed);
    }

    
protected:
    
    std::atomic<future<T> *>_owner;
};

template<typename T>
class promise: public promise_base<T> {
public:
    using promise_base<T>::promise_base;
    
    ///set value
    /**
     * @param x value to be set
     */
    void set_value(T &&x)  {
        auto m = this->claim();
        if (m) m->set_value(std::move(x));
    }
    
    ///set value
    /**
     * @param x value to be set
     */
    void set_value(const T &x) {
        auto m = this->claim();
        if (m) m->set_value(x);
    }
    
    
    ///promise can be used as callback function
    void operator()(T &&x)  {
        set_value(std::move(x));
    }
    ///promise can be used as callback function
    void operator()(const T &x)  {
        set_value(x);
    }
};

template<>
class promise<void>: public promise_base<void> {
public:
    
    using promise_base<void >::promise_base;

    ///makes future ready
    /**
     */
    void set_value() {
        auto m = this->claim();
        if (m) m->set_value();
    }
    ///you can call promise as callback
    void operator()() {
        set_value();
    }

};

///Promise with default value
template<typename T>
class promise_with_default: public promise<T> {
public:
    using promise<T>::promise;
    
    template<typename ... Args>
    promise_with_default(promise<T> &&prom, Args &&... args)
        :promise<T>(std::move(prom)),def(std::forward<Args>(args)...) {}
    ~promise_with_default() {
        this->set_value(std::move(def));
    }
protected:
    T def;
    
};

///Futures with callback function
/**
 * When future is resolved a callback function i called
 * @tparam T type of value
 * @tparam Fn function type
 * 
 * This class is intended to be used in classes as member variables, to avoid
 * memory allocation - because the future must be allocated somewhere. 
 * 
 * If you have no such place, use more convenient function make_promise
 * 
 * @see make_promise()
 */
template<typename T, typename Fn>
class future_with_cb: public future<T>, public abstract_awaiter<false>, public coro_promise_base {
public:
    
    ///Construct a future and pass a callback function
    future_with_cb(Fn &&fn):_fn(std::forward<Fn>(fn)) {
        this->_awaiter = this;
    }
    virtual void resume() noexcept override {
        _fn(*this);
        delete this;
    }

    virtual ~future_with_cb() = default;
    
protected:
    Fn _fn;

};

///Extends the future_with_cb with ability to be allocated in a storage
template<typename T, typename Storage, typename Fn>
class future_with_cb_no_alloc: public future_with_cb<T, Fn> {
public:
    using future_with_cb<T, Fn>::future_with_cb;
    
    void *operator new(std::size_t sz, Storage &m) {
        return m.alloc(sz);
    }
    void operator delete(void *ptr, Storage &m) {
        
    }
    
    void operator delete(void *ptr, std::size_t) {
        
    }
private:
    void *operator new(std::size_t sz);

};


/**@{*/
///Makes callback promise
/**
 * Callback promise cause execution of the callback when promise is resolved.,
 * This function has no use in coroutines, but it can allows to use promises in normal 
 * code. Result object is normal promise.  
 * 
 * There is also only one memory allocation for whole promise and the callback function. 
 * 
 * @tparam T type of promise
 * @param fn callback function. Once the promise is resolved, the callback function receives
 * whole future<T> object as argument (as reference). It can be used to retrieve the value from it
 * 
 * @return promise<T> object 
 * 
 * @note callback is executed only after all instances of the promise are destroyed. This helps
 * to reduce side-effect which could potential happen during setting the value of the promise. So
 * execution is postponed. However ensure, that when promise is resolved, the promise instance 
 * is being destroyed as soon as possible
 * 
 * @see future<T>::get()
 */
template<typename T, typename Fn>
promise<T> make_promise(Fn &&fn) {
    auto f = new future_with_cb<T, Fn>(std::forward<Fn>(fn));
    return f->get_promise();
}


template<typename T, typename Fn, typename Storage>
promise<T> make_promise(Fn &&fn, Storage &storage) {
    auto f = new(storage) future_with_cb_no_alloc<T, Storage, Fn>(std::forward<Fn>(fn));
    return f->get_promise();
}
/**@}*/


///Shared promise
/**
 * @tparam T
 */
template<typename T, typename shared_place  >
class shared_promise_base {

public:    
    void release() {
        _ptr->release();
    }
    
    operator bool() const {
        return _ptr && _ptr->_fut.load(std::memory_order_relaxed) != nullptr;
    }

    ///Returns true, if the promise is not valid
    bool operator !() const {
        return !operator bool();
    }

    ///capture current exception
    void unhandled_exception() const {
        _ptr->unhandled_exception();
    }

    shared_promise_base();
    template<typename ... Args>
    explicit shared_promise_base(promise<T> &&p, Args && ... args)
        :_ptr(std::make_shared<shared_place>(std::move(p), std::forward<Args>(args)...)) {}

    /*
     *     │                                                       ┌────────────────┐
     *     │owner──────────┐                         ┌─shared_ptr──┤ shared_promise │
     *     │               ▼                         │             └────────────────┘
     *     │           ┌────────┐       allocated    │
     *     ▼           │        │      ┌─────────┐   │             ┌────────────────┐
     * co_await ◄──────┤ future │◄─────┤ promise │◄──┼─shared_ptr──┤ shared_promise │
     *     │           │        │      │         │   │             └────────────────┘
     *     │           └────────┘      └─────────┘   │
     *     │                                         │            ┌──────────────────┐
     *     │                                         │            │┌────────────────┐│
     *     ▼                                         └─shared_ptr─││ shared_promise ││
     *                                                            │└────────────────┘│
     *                                                            │       ▲          │
     *                                                            │       │          │   ┌─────────┐
     *                                                            │     future    ◄──┼───┤ promise │
     *                                                            └──────────────────┘   └─────────┘
     *                                                                      allocated
     */

protected:
    std::shared_ptr<shared_place > _ptr;
};

template<typename T, typename shared_place >
class shared_promise: public shared_promise_base<T, shared_place> {
public:
    using shared_promise_base<T, shared_place>::shared_promise_base;
    
    void set_value(T &&x)  {
        this->_ptr->set_value(std::move(x));
    }
    
    ///set value
    /**
     * @param x value to be set
     */
    void set_value(const T &x) {
        this->_ptr->set_value(x);
    }
    
    ///promise can be used as callback function
    void operator()(T &&x)  {
        set_value(std::move(x));
    }
    ///promise can be used as callback function
    void operator()(const T &x)  {
        set_value(x);
    }

    explicit operator promise<T>() {
        return make_promise<T>([p = *this>(this)](cocls::future<T> &f){
            try {
                p.set_value(std::move(f.get()));
            } catch (...) {
                p.unhandled_exception();
            }
        });
    }
    
};

template<typename shared_place>
class shared_promise<void, shared_place>: public shared_promise_base<void, shared_place> {
public:
    
    using shared_promise_base<void , shared_place>::shared_promise_base;

    ///makes future ready
    /**
     */
    void set_value() {
        this->_ptr->set_value();
    }
    ///you can call promise as callback
    void operator()() {
        set_value();
    }
    explicit operator promise<void>() {
        return make_promise<void>([p = *this](cocls::future<void> &f){
            try {
                p.set_value();
            } catch (...) {
                p.unhandled_exception();
            }
        });
    }
    
};



template<typename T>
inline shared_promise<T> cocls::future_base<T>::get_shared_promise() {
    return shared_promise<T>(get_promise());
}

}
#endif /* SRC_COCLASSES_FUTURE_H_ */

