/**
 * @file future.h
 */
#pragma once
#ifndef SRC_COCLASSES_FUTURE_H_
#define SRC_COCLASSES_FUTURE_H_

#include "awaiter.h"
#include "common.h"
#include "exceptions.h"




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
 *  To set the variable, you need to retrieve a promise object as first. There can
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
 *  Future can be awaited by multiple awaiters. However you need to ensure MT
 *  safety by proper synchronization. For example when there are multiple awaiters,
 *  ensure, that no awaiter wants to move the result outside of future. Also ensure,
 *  that future can't be destroyed after it is awaited. For multiple awaiting
 *  is recommended to use make_shared
 *  
 *  
 *  @note Once promise is obtained, the future must be resolved. Destroying the
 *  future in such case is UB - will probably ends by crashing code during
 *  setting the value of such promise. 
 *  
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
    using awaiter = abstract_awaiter<true>;
    
    future_base()
        :_awaiter(&empty_awaiter<true>::disabled)
    {}
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
        abstract_awaiter<true> *p  = &empty_awaiter<true>::disabled;
        if (_awaiter.compare_exchange_strong(p, nullptr, std::memory_order_relaxed)) {
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
     * @return copyable promise
     */
    shared_promise<T> get_shared_promise();

    
    ///Starts waiting for a result
    co_awaiter<future<T>,true>  operator co_await() {
        return *static_cast<future<T> *>(this);
    }
    
    ///Wait synchronously
    /**
     * @return the value of the future
     */
    decltype(auto) wait() {
        return co_awaiter<future<T>,true >(*static_cast<future<T> *>(this)).wait();
    }

        

    
protected:

    friend class co_awaiter<future<T>, true>;

    
    mutable std::atomic<awaiter *> _awaiter = nullptr;
    
    bool is_ready() {
        return awaiter::is_ready(_awaiter);        
    }
    
    bool subscribe_awaiter(abstract_awaiter<true> *x) {
        return x->subscibre_check_ready(_awaiter);
    }
    
    decltype(auto) get_result() {
        return static_cast<future<T> *>(this)->get(); 
    }
    
    
    friend class promise_base<T>;
    friend class promise<T>;
    
};



template<typename T>
class future: public future_base<T> {
public:
    future() {}
protected:
    friend class future_base<T>;
    friend class promise_base<T>;
    friend class promise<T>;
    using awaiter = typename future_base<T>::awaiter;

    
    union {
        T _v;
        std::exception_ptr _e;
    };

    void unhandled_exception() {
        new(&_e) std::exception_ptr(std::current_exception());
        awaiter::mark_ready_exception_resume(this->_awaiter);
    }      
    
    template<typename X>
    auto set_value(X &&x) -> std::enable_if_t<std::is_convertible_v<X, T> > {
        new(&_v) T(std::forward<X>(x));
        awaiter::mark_ready_data_resume(this->_awaiter);
    }
public:
    ///get value
    /**
     * 
     * @return value of the future
     * @exception value_not_ready_exception when value is not ready
     */
    T &get() {
        if (awaiter::mark_processed_data(this->_awaiter)) return _v;
        if (awaiter::mark_processed_exception(this->_awaiter)) std::rethrow_exception(_e);
        throw value_not_ready_exception();
        
    }
    ///get value
    /**
     * 
     * @return value of the future
     * @exception value_not_ready_exception when value is not ready
     */
    const T & get() const {
        if (awaiter::mark_processed_data(this->_awaiter)) return _v;
        if (awaiter::mark_processed_exception(this->_awaiter)) std::rethrow_exception(_e);
        throw value_not_ready_exception();
    }

    
    ~future() {
        //future must be not initialized or resolved to be destroyed
        assert(this->_awaiter.load(std::memory_order_relaxed) == &empty_awaiter<true>::disabled 
                || awaiter::is_ready(this->_awaiter));
        awaiter::cleanup_by_mark(this->_awaiter,[this](){
                _v.~T();
            },[this](){
                _e.~exception_ptr();
            });
    }

};

template<>
class future<void>: public future_base<void> {
public:
    future() {}
protected:
    friend class future_base<void>;
    friend class promise_base<void>;
    friend class promise<void>;

    
    std::exception_ptr _e;
    
    void set_value() {
        awaiter::mark_ready_data_resume(_awaiter);
    }

    void unhandled_exception() {
        _e = std::exception_ptr(std::current_exception());
        awaiter::mark_ready_exception_resume(_awaiter);
    }
    
    
public:
    ///get value
    /**
     * 
     * @return value of the future
     * @exception value_not_ready_exception when value is not ready
     */
    void get() const {
        if (awaiter::mark_processed_data(_awaiter)) return ;
        if (awaiter::mark_processed_exception(_awaiter)) std::rethrow_exception(_e);
        throw value_not_ready_exception();
        
    }
    
    ~future() {
        //future must be not initialized or resolved to be destroyed
        assert(this->_awaiter.load(std::memory_order_relaxed) == &empty_awaiter<true>::disabled 
                || awaiter::is_ready(_awaiter));
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
    void release() const {
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
    future<T> *claim() const {
        return _owner.exchange(nullptr, std::memory_order_relaxed);
    }

    
protected:
    
    mutable std::atomic<future<T> *>_owner;
};

template<typename T>
class promise: public promise_base<T> {
public:
    using promise_base<T>::promise_base;
    
    ///set value
    /**
     * @param x value to be set
     */
    void set_value(T &&x) const  {
        auto m = this->claim();
        if (m) m->set_value(std::move(x));
    }
    
    ///set value
    /**
     * @param x value to be set
     */
    void set_value(const T &x) const {
        auto m = this->claim();
        if (m) m->set_value(x);
    }
    
    
    ///promise can be used as callback function
    void operator()(T &&x)  const {
        set_value(std::move(x));
    }
    ///promise can be used as callback function
    void operator()(const T &x)  const {
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
    void set_value() const {
        auto m = this->claim();
        if (m) m->set_value();
    }
    ///you can call promise as callback
    void operator()() const {
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
    promise_with_default(promise_with_default &&other) = default;
    promise_with_default &operator=(promise_with_default &&other) {
        if (this != &other) {
            promise<T>::operator=(std::move(other));
            def = std::move(def);
        }
        return *this;
    }
protected:
    T def;
    
};

///Promise with default value
/**
 * @tparam T type, must be integral type
 * @tparam val default value
 */
template<typename T, T val>
class promise_with_default_v: public promise<T> {
public:
    using promise<T>::promise;
    promise_with_default_v() = default;
    promise_with_default_v(promise_with_default_v &&other) = default;
    promise_with_default_v &operator=(promise_with_default_v &&other) = default;
    ~promise_with_default_v() {
        this->set_value(val);
    }
    promise_with_default_v(promise<T> &&p):promise<T>(std::move(p)) {}
};

///Promise with default value - constant is specified in template paramater
/**
 * @tparam T type 
 * @tparam val const pointer to default value, must have external linkage
 */
template<typename T, const T *val>
class promise_with_default_vp: public promise<T> {
public:
    using promise<T>::promise;
    promise_with_default_vp() = default;
    promise_with_default_vp(promise_with_default_vp &&other) = default;
    promise_with_default_vp &operator=(promise_with_default_vp &&other) = default;
    ~promise_with_default_vp() {
        this->set_value(*val);
    }
    promise_with_default_vp(promise<T> &&p):promise<T>(std::move(p)) {}
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
class future_with_cb: public future<T>, public abstract_awaiter<true>, public coro_promise_base {
public:
    
    ///Construct a future and pass a callback function
    future_with_cb(Fn &&fn):_fn(std::forward<Fn>(fn)) {
        this->_awaiter = this;
    }
    virtual void resume() noexcept override {
        _fn(*this);
        delete this;
    }
    promise<T> get_promise() {
        return promise<T>(*this);
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

