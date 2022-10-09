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
        return *static_cast<future<T> *>(this);
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
    abstract_awaiter<future<T> > *_awaiter = nullptr;
    value_or_exception<T> _value;

    void add_ref() {
        _pcount.fetch_add(1, std::memory_order_relaxed);
    }
   
    void release_ref() {
        if (_pcount.fetch_sub(1, std::memory_order_release)-1 == 0) {            
            if (_awaiter) {
                _awaiter->resume();
            }
        }
    }
    
    bool is_ready() {
        return _value.is_ready() && _pcount == 0;        
    }
    
    bool subscribe_awaiter(abstract_awaiter<future<T> > *x) {
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
    promise_base(future<T> &fut):_owner(&fut) {_owner->add_ref();}
    promise_base(const promise_base &other):_owner(other._owner) {if (_owner) _owner->add_ref();}
    promise_base(promise_base &&other):_owner(other._owner) {other._owner = nullptr;}
    ~promise_base() {if (_owner) _owner->release_ref();}
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
 * @param buffer pointer to storage, if the future object is smaller enough to fit into
 * the buffer. 
 * @param size of the buffer
 * 
 * @return promise<T> object 
 * 
 * @note callback is executed only after all instances of the promise are destroyed. This helps
 * to reduce side-effect which could potential happen during setting the value of the promise. So
 * execution is postponed. However ensure, that when promise is resolved, the promise instance 
 * is being destroyed as soon as possible
 */
template<typename T, typename Fn>
promise<T> make_promise(Fn &&fn, void *buffer = nullptr, std::size_t sz = 0) {
    
        
    ///Storage for coroutine containing a callback
    /** the calculation should be improved depend on final compiler 
     * because there is no way how to determine size of coroutine frame
     * in compile time
     */
        
    
    class futimpl: public future<T>, public abstract_awaiter<future<T>,false> {
    public:
        futimpl(Fn &&fn):_fn(std::forward<Fn>(fn)) {
            this->_awaiter = this;
        }
        virtual void resume() override {
            _fn(*this);
            delete this;
        }
        virtual ~futimpl() = default;
        
    protected:
        Fn _fn;

    };
    
    class futimpl_inl: public futimpl {
    public:
        using futimpl::futimpl;
        
        void *operator new(std::size_t, void *p) {return p;}
        void operator delete(void *, void *) {}
        void operator delete(void *, std::size_t) {}
    };
    
    if (sz < sizeof(futimpl_inl)) {
        auto f = new futimpl(std::forward<Fn>(fn));
        return f->get_promise();
    } else {
        auto f = new(buffer) futimpl_inl(std::forward<Fn>(fn));
        return f->get_promise();
    }
}



}
#endif /* SRC_COCLASSES_FUTURE_H_ */
