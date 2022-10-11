#pragma once
#ifndef SRC_COCLASSES_FUTURE_H_
#define SRC_COCLASSES_FUTURE_H_

#include "common.h"
#include "resume_lock.h"
#include "exceptions.h"

#include "sync_await.h"

#include "reusable.h"

#include "value_or_exception.h"

#include <assert.h>
#include <memory>
#include <mutex>
#include <condition_variable>



namespace cocls {


template<typename T>
class future;
template<typename T>
class promise;
template<typename T>
class promise_base;


template<typename T>
class future_base {
public:
    promise<T> get_promise() {
        return promise<T>(*static_cast<future<T> *>(this));
    }
    
    co_awaiter<future<T> >  operator co_await() {
        return *static_cast<future<T> *>(this);
    }
    
    auto wait() {
        return blocking_awaiter<future<T> >(*static_cast<future<T> *>(this)).wait();
    }
    
    auto get() {
        return _value.get_value();
    }
    
protected:
    
    std::atomic<unsigned int> _pcount = 0;
    abstract_awaiter<> *_awaiter = nullptr;
    value_or_exception<T> _value;

    void add_ref() {
        _pcount.fetch_add(1, std::memory_order_relaxed);
    }
   
    void release_ref() {
        unsigned int x = _pcount.fetch_sub(1, std::memory_order_release)-1;
        assert(x < static_cast<unsigned int>(-10));
        if (x == 0) {            
            if (_awaiter) {
                _awaiter->resume();
            }
        }
    }
    
    bool is_ready() {
        return _value.is_ready() && _pcount == 0;        
    }
    
    bool subscribe_awaiter(abstract_awaiter<> *x) {
        ++_pcount;
        _awaiter = x;
        return  (--_pcount > 0 || !_value.is_ready());
    }
    
    auto get_result() {
        return _value.get_value();
    }
    
    
    friend class promise_base<T>;
    friend class promise<T>;
    friend class abstract_owned_awaiter<future<T> >;
    friend class co_awaiter<future<T> >;
    friend class blocking_awaiter<future<T> >;
    
    void unhandled_exception() {
        _value.unhandled_exception();
    }
};

template<typename T>
class future: public future_base<T> {
public:
    
    template<typename X>
    auto set_value(X &&x) -> decltype(std::declval<value_or_exception<T> >().set_value(x)) {
        this->_value.set_value(std::forward<X>(x));
    }
};
template<>
class future<void>: public future_base<void> {
public:
    
    auto set_value() {
        this->_value.set_value();
    }
};



template<typename T>
class promise_base {
public:
    promise_base():_owner(nullptr) {}
    explicit promise_base(future<T> &fut):_owner(&fut) {_owner->add_ref();}
    promise_base(const promise_base &other):_owner(other._owner) {if (_owner) _owner->add_ref();}
    promise_base(promise_base &&other):_owner(other._owner) {other._owner = nullptr;}
    ~promise_base() {
        release();
    }
    promise_base &operator=(const promise_base &other) {
        if (this != &other) {
            if (_owner) _owner->release_ref();
            _owner = other._owner;
            if (_owner) _owner->add_ref();
                    
        }
        return *this;
    }
    promise_base &operator=(promise_base &&other) {
        if (this != &other) {
            if (_owner) _owner->release_ref();
            _owner = other._owner;
            other._owner = nullptr;
        }
        return *this;
    }
    
    ///Release promise object, decreases count of references, can resume associated coroutine
    void release() {
        auto m = _owner;
        _owner = nullptr;
        if (m) m->release_ref();        
    }



    ///Returns true, if the promise is valid
    operator bool() const {
        return _owner != nullptr;
    }

    ///Returns true, if the promise is not valid
    bool operator !() const {
        return _owner == nullptr;
    }

protected:
    future<T> *_owner;
};

///Promise object
/**
 * Promise object acts as some reference to its future. You can copy and move promise object. 
 * You can have multiple promise objects at time, but only one such object can accept value at 
 * the time - this is not MT Safe.
 * 
 * Note that awaiting coroutine is resumed when last promise is destroyed. Setting value doesn't
 * perform resumption, only sets value
 * 
 * @tparam T
 */
template<typename T>
class promise: public promise_base<T> {
public:
    using promise_base<T>::promise_base;
    
    ///set value
    void set_value(T &&x) const {
        this->_owner->set_value(std::move(x));
    }
    
    ///set value
    void set_value(const T &x) const {
        this->_owner->set_value(x);
    }
    
    ///capture current exception
    void unhandled_exception() const {
        this->_owner->set_exception(std::current_exception());
    }
    
    ///promise can be used as callback function
    void operator()(T &&x) {
        set_value(std::move(x));
    }
    ///promise can be used as callback function
    void operator()(const T &x) {
        set_value(x);
    }
};

///Promise object
/**
 * Promise object acts as some reference to its future. You can copy and move promise object. 
 * You can have multiple promise objects at time, but only one such object can accept value at 
 * the time - this is not MT Safe.
 * 
 * Note that awaiting coroutine is resumed when last promise is destroyed. Setting value doesn't
 * perform resumption, only sets value
 */

template<>
class promise<void>: public promise_base<void> {
public:
    
    using promise_base<void >::promise_base;

    ///makes future ready
    void set_value() {
        _owner->set_value();
    }
    ///capture unhandled exception
    void unhandled_exception() {
        _owner->unhandled_exception();
    }
    ///
    void operator()() {
        set_value();
    }

};


///Futures with callback function
/**
 * When future is resolved a callback function i called
 * @tparam T type of value
 * @tparam Fn function type
 */
template<typename T, typename Fn>
class future_with_cb: public future<T>, public abstract_awaiter<false> {
public:
    future_with_cb(Fn &&fn):_fn(std::forward<Fn>(fn)) {
        this->_awaiter = this;
    }
    virtual void resume() override {
        _fn(*this);
        delete this;
    }
    virtual ~future_with_cb() = default;
    
protected:
    Fn _fn;

};

template<typename T, typename Fn>
class future_with_cb_reusable: public future_with_cb<T, Fn> {
public:
    using future_with_cb<T, Fn>::future_with_cb;
    
    template<typename Storage>
    void *operator new(std::size_t sz, reusable_memory<Storage> &m) {
        return m.alloc(sz);
    }
    template<typename Storage>
    void operator delete(void *ptr, reusable_memory<Storage> &m) {
        m.dealloc(ptr);
    }
    
    void operator delete(void *ptr, std::size_t) {
        reusable_memory<void>::generic_delete(ptr);
    }
};


///Makes callback promise
/**
 * Callback promise cause execution of the callback when promise is resolved.,
 * This function has no use in coroutines, but it can allows to use promises in normal 
 * code. Result object is normal promise.  
 * 
 * There is also only one memory allocation for whole promise and the callback functuon. 
 * 
 * @tparam T type of promise
 * @param fn callback function. Once the promise is resolved, callback function receives
 * whole future<T> object as argument (as reference). It can retrieve the value from it
 * 
 * @return promise<T> object 
 * 
 * @note callback is executed only after all instances of the promise are destroyed. This helps
 * to reduce side-effect which could potential happen during setting the value of the promise. So
 * execution is postponed. However ensure, that when promise is resolved, the promise instance 
 * is being destroyed as soon as possible
 */
template<typename T, typename Fn>
promise<T> make_promise(Fn &&fn) {
    auto f = new future_with_cb<T, Fn>(std::forward<Fn>(fn));
    return f->get_promise();
}

template<typename T, typename Fn, typename Storage>
promise<T> make_promise(Fn &&fn, reusable_memory<Storage> &storage) {
    auto f = new(storage) future_with_cb_reusable<T, Fn>(std::forward<Fn>(fn));
    return f->get_promise();
}


}
#endif /* SRC_COCLASSES_FUTURE_H_ */
