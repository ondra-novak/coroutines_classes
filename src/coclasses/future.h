/**
 * @file future.h
 */
#pragma once
#ifndef SRC_COCLASSES_FUTURE_H_
#define SRC_COCLASSES_FUTURE_H_

#include "common.h"
#include "exceptions.h"



#include "value_or_exception.h"

#include "abstract_awaiter.h"

#include "poolalloc.h"
#include "resume_ctl.h"

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
    ///Create a promise
    /**
     * @return a associated promise object. This object can be moved, copied,
     * assigned, etc. 
     * 
     * @note you have to destroy all created/copied instances to allow future
     * to resume the awaiting coroutine. So it is not a good idea to store
     * the future object in variable declared in current coroutine frame. Move the
     * variable out! 
     * 
     */
    promise<T> get_promise() {
        return promise<T>(*static_cast<future<T> *>(this));
    }
    
    ///Starts waiting for a result
    co_awaiter<future<T> >  operator co_await() {
        return *static_cast<future<T> *>(this);
    }
    
    ///Wait synchronously
    /**
     * @return the value of the future
     */
    auto wait() {
        return blocking_awaiter<future<T> >(*static_cast<future<T> *>(this)).wait();
    }
    
    ///get value
    /**
     * 
     * @return value of the future
     * @exception value_not_ready_exception when value is not ready
     */
    auto get() {
        return _value.get_value();
    }
    ///get value
    /**
     * 
     * @return value of the future
     * @exception value_not_ready_exception when value is not ready
     */
    auto get() const {
        return _value.get_value();
    }
    
protected:
    
    std::atomic<unsigned int> _pcount = 0;
    abstract_awaiter<> *_awaiter = nullptr;
    value_or_exception<T> _value;
    std::atomic<bool> _ready_flag = false;

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
        return _ready_flag &&  _pcount == 0;        
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
    friend class co_awaiter_base<future<T> >;
    friend class blocking_awaiter<future<T> >;
    
    void unhandled_exception() {
        if (_ready_flag.exchange(true) == false) {
            _value.unhandled_exception();
        }
    }
};



///Future object
/**
 * Future object - future variable - is variable which will have a value in
 * future. The object has two states. The first state, when the value is not
 * yet known, and second state, when value is already known and the future is 
 * "resolved".
 * 
 * @tparam T type of stored value
 * 
 * The future can also store exception if the value cannot be obtained. This
 * allows to propagate an exception from the producer to the consument.
 * 
 * Future object can be awaited.
 * 
 * Along with the future object, there is a promise object. Promise is
 * a satellite object, which can be passed to the procuder and which can
 * collect the result for the future. To obtain promise object call get_promise()
 * 
 * Future is fundamental part of coroutine library, as it provides connection
 * between non-coroutine producers and coroutine consuments, because producers
 * can be more system-oreinted code, where coroutines are not allowed. For example
 * you can have a network monitor, which notifies about network events. Once such
 * event is notified, am associated awaiting coroutine can be resumed and retrieve
 * such an event.
 * 
 * @note Futures are one-shot and they can't be rearmed. You need to destroy
 * and create new instance to 'rearm' the future
 * 
 * 
 * @see make_promise
 * 
 */
template<typename T>
class future: public future_base<T> {
public:
    
    template<typename X>
    auto set_value(X &&x) -> decltype(std::declval<value_or_exception<T> >().set_value(x), true) {
        if (this->_ready_flag.exchange(true) == false) {
            this->_value.set_value(std::forward<X>(x));
            return true;
        }
        return false;
            
    }
};

///Future object without value, it just records 'happening' of an event
/**
 * @copydoc future<T>
 */
template<>
class future<void>: public future_base<void> {
public:
    
    auto set_value() {
        if (_ready_flag.exchange(true) == false) {
            this->_value.set_value();
            return true;
        }
        return false;
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

    ///capture current exception
    /**
     * @note need to be in catch handler
     * @retval true exception captured
     * @retval false exception was not captured, there is no exception or future is already resolved
     */
    bool unhandled_exception() const {
        auto exp = std::current_exception();
        if (exp) {
            return this->_owner->set_exception(std::current_exception());
        } else {
            return false;
        }
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
    /**
     * @param x value to be set
     * @retval true value set
     * @retval false future is already resolved
     */
    bool set_value(T &&x) const {
        return this->_owner->set_value(std::move(x));
    }
    
    ///set value
    /**
     * @param x value to be set
     * @retval true value set
     * @retval false future is already resolved
     */
    bool set_value(const T &x) const {
        return this->_owner->set_value(x);
    }
    
    
    ///promise can be used as callback function
    void operator()(T &&x) const {
        set_value(std::move(x));
    }
    ///promise can be used as callback function
    void operator()(const T &x) const {
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
    /**
     * @retval true success
     * @retval false already resolved 
     */
    bool set_value() const {
        return _owner->set_value();
    }
    ///you can call promise as callback
    void operator()() const{
        set_value();
    }

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
        m.dealloc(ptr);
    }
    
    void operator delete(void *ptr, std::size_t) {
        Storage::dealloc(ptr);
    }
};


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

///Makes callback promise
/** 
 * @copydoc make_promise
 * 
 * @param storage specify storage where the internal object will be allocated
 */

template<typename T, typename Fn, typename Storage>
promise<T> make_promise(Fn &&fn, Storage &storage) {
    auto f = new(storage) future_with_cb_no_alloc<T, Storage, Fn>(std::forward<Fn>(fn));
    return f->get_promise();
}


}
#endif /* SRC_COCLASSES_FUTURE_H_ */
