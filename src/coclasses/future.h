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


template<typename T>
class future;
template<typename T>
class promise;
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
    
protected:
    
    std::atomic<unsigned int> _pcount = 0;
    abstract_awaiter<> *_awaiter = nullptr;
    future_var<T> _value;    

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
        return _value.has_value() &&  _pcount == 0;        
    }
    
    bool subscribe_awaiter(abstract_awaiter<> *x) {
        ++_pcount;
        _awaiter = x;
        return  (--_pcount > 0 || !_value.has_value());
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
    auto set_value(X &&x) -> decltype(std::declval<future_var<T> >().emplace(x)) {
        this->_value.emplace(std::forward<X>(x));
    }
};

///Future object without value, it just records 'happening' of an event
/**
 * @copydoc future<T>
 */
template<>
class future<void>: public future_base<void> {
public:
    
    void set_value() {
        this->_value.emplace();
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
    void unhandled_exception() const {
        this->_owner->unhandled_exception();
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
     */
    void set_value(T &&x) const {
        this->_owner->set_value(std::move(x));
    }
    
    ///set value
    /**
     * @param x value to be set
     */
    void set_value(const T &x) const {
        this->_owner->set_value(x);
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
     */
    void set_value() const {
        _owner->set_value();
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


///Future object, which can be resolved from multiple sources - first source resolves future, others are ignored
/**
 * 
 * @tparam T
 */
template<typename T>
class multi_source_future {
public:
        
    multi_source_future();
    multi_source_future(multi_source_future &&) = default;
    multi_source_future(const multi_source_future &) = delete;
    multi_source_future &operator=(const multi_source_future &) = delete;
    multi_source_future &operator=(multi_source_future &&) = delete;
    
    ///await for result - only one awaiter is allowed
    co_awaiter<future<T> > operator co_await();

    ///wait synchronously, return result
    decltype(auto) wait() {
        return operator co_await().wait();
    }

    ///Retrieve promise object
    /**
     * @note Object can be copied or moved, but it still belongs to one
     * source. If you need to get promise for two sources, you need to call this function
     * twice.
     * 
     * Setting value is MT safe if it set for different source simultaneously. It is not
     * MT safe to set value to promises from single source.  
     * 
     * @return
     */
    promise<T> get_promise();
 
protected:
    
    struct storage {
        future<T> _fut;
        std::atomic<bool> _loading;        
    };
    
    
    using storage_p = std::shared_ptr<storage>;
    storage_p _ptr;
    
};



template<typename T>
inline multi_source_future<T>::multi_source_future()
:_ptr(std::make_shared<storage>())
{
}

template<typename T>
inline co_awaiter<future<T> > multi_source_future<T>::operator co_await() {
    return _ptr->_fut.operator co_await();
}

template<typename T>
inline promise<T> multi_source_future<T>::get_promise() {
    return make_promise<T>([ptr = this->_ptr](future<T> &f){
        if (ptr->_loading.exchange(true) == false) {
            promise<T> p = ptr->_fut.get_promise();
            try {
                p.set_value(std::move(f.get()));
            } catch (...) {
                p.unhandled_exception();
            }
        }
    });
}

}

#endif /* SRC_COCLASSES_FUTURE_H_ */

