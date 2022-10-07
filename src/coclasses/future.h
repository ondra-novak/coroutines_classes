#pragma once
#ifndef SRC_COCLASSES_FUTURE_H_
#define SRC_COCLASSES_FUTURE_H_

#include "common.h"
#include "resume_lock.h"
#include "exceptions.h"

#include "sync_await.h"
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


template<typename T, typename Impl>
class future_base {
public:
    
    future_base():_pcount(0),_ready(false), _awaiter(nullptr) {}
    ~future_base() {
        assert(_pcount == 0);
    }
    
    future_base(const future_base &) = delete;
    future_base &operator=(const future_base &) = delete;
    
    bool is_ready() {
        return _ready.load(std::memory_order_acquire);
    }

    

    
    class awaiter {
    public:
        awaiter(Impl &owner):_owner(owner) {}
        virtual ~awaiter() = default;
        awaiter(const awaiter &) = default;
        awaiter &operator=(const awaiter &) = delete;

        std::coroutine_handle<> await_suspend(handle_t h) {
            this->_owner.add_ref();
            this->_owner._awaiter = h;
            this->_owner.release_ref();
            return resume_lock::await_suspend();
        }
        bool await_ready() const noexcept {
            return _owner._ready && _owner._pcount.load(std::memory_order_relaxed) == 0;
        }

    protected:
        Impl &_owner;
    };



protected:    
    std::atomic<unsigned int> _pcount;
    std::atomic<bool> _ready;
    handle_t _awaiter;
    

    void add_ref() {
        _pcount.fetch_add(1, std::memory_order_relaxed);
    }
   
    void release_ref() {
        if (_pcount.fetch_sub(1, std::memory_order_release)-1 == 0) {            
            if (_awaiter) {
                if (!this->_ready.load(std::memory_order_acquire)) {
                    static_cast<Impl *>(this)->set_exception(std::make_exception_ptr(await_canceled_exception()));
                }
                resume_lock::resume(_awaiter);
            }
        }
    }

};

///Creates future variable for coroutine
/**
 * Future variable can be used in coroutine and can be awaited.
 * @tparam T type of variable. It can be void
 * 
 * variable cannot be moved or copied
 */
template<typename T>
class future: public future_base<T, future<T> > {
public:

    using super_t = future_base<T, future<T> >;
    
    future():_is_exception(false) {}
    ~future() {
        if (this->is_ready()) {
            if (_is_exception) _exception.~exception_ptr();
            else _value.~T();
        }
    }
    future(const future &) = delete;
    future &operator=(const future &) = delete;
    
    class awaiter: public super_t::awaiter {
    public:
        awaiter(future &owner):super_t::awaiter(owner) {}
        T &await_resume() {
            if (this->_owner._is_exception) std::rethrow_exception(this->_owner._exception);
            else return this->_owner._value;
        }
    };
    

    ///future can be awaited
    awaiter operator co_await() {
        return awaiter(*this);
    }

    
    T &wait() {
        return sync_await(*this);
    }

   
    
    ///Retrieves promise object
    /**
     * Use this object to set value of the future. 
     * 
     * @return promise object. It can be copied and moved
     * 
     * @note co_await resumes coroutine when the last promise is destroyed.
     */
    promise<T> get_promise();

    ///Get value - only if the future is already resolved
    T &get() {
        if (!this->is_ready()) throw value_not_ready_exception();
        if (this->_is_exception) std::rethrow_exception(this->_exception);
        return this->_value;
    }


protected:
     friend class future_base<T, future<T> >;
    
     bool _is_exception;
     union {
         T _value;
         std::exception_ptr _exception;
     };
     
     void set_exception(std::exception_ptr &&e) {
         if (!this->is_ready()) {
             new(&_exception) std::exception_ptr(std::move(e));
             _is_exception = true;
             this->_ready.store(true, std::memory_order_release);
             
         }
     }
     
     template<typename X>
     void set_value(X &&value) {
         if (!this->is_ready()) {
             new(&_value) T(std::forward<X>(value));
             _is_exception = false;
             this->_ready.store(true, std::memory_order_release);
             
         }
     }

     friend class promise<T>;
     friend class promise_base<T>;

    
};

///Creates future variable for coroutine - void specialization
/**
 * This future value has no exact value. But it still can be resolved and can capture exception
 */
template<>
class future<void>: public future_base<void, future<void> > {
public:
    using super_t = future_base<void, future<void> >;

    
    future():_exception(nullptr) {}
    future(const future &) = delete;
    future &operator=(const future &) = delete;

    class awaiter:public super_t::awaiter {
    public:
        awaiter(future &owner):super_t::awaiter(owner) {}

        void await_resume() {
            if (this->_owner._exception) std::rethrow_exception(this->_owner._exception);            
        }
    };
    
    awaiter operator co_await() {
        return awaiter(*this);
    }

    ///Check state of future, throws exception if there is such exceptional state
    void get() {
        if (!this->is_ready()) throw value_not_ready_exception();
        if (this->_exception) std::rethrow_exception(this->_exception);
    }


    void wait() {
        sync_await(*this);
    }

    
    promise<void> get_promise();

protected:
    
    
     std::exception_ptr _exception;
     

     friend class future_base<void, future<void> >;
     friend class promise<void>;
     friend class promise_base<void>;

     void set_exception(std::exception_ptr &&e) {
         if (!_ready) {
             _exception = e;
             _ready.store(true, std::memory_order_release);
             
         }
     }
     
     void set_value() {
         if (!_ready) {
             _exception = nullptr;
             _ready.store(true, std::memory_order_release);
             
         }
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
    
    ///set exception
    void set_exception(std::exception_ptr &&e) const {
        this->_owner->set_exception(std::move(e));
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
    ///sets exception
    void set_exception(std::exception_ptr &&e) {
        _owner->set_exception(std::move(e));
    }
    ///capture unhandled exception
    void unhandled_exception() {
        _owner->set_exception(std::current_exception());
    }
    ///
    void operator()() {
        set_value();
    }

};


template<typename T>
inline promise<T> future<T>::get_promise()  {
    return promise<T>(*this);
}

inline promise<void> future<void>::get_promise()  {
    return promise<void>(*this);
}

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
    
    class futimpl: public future<T>, public abstract_resumable_t {
    public:

        virtual std::coroutine_handle<> resume() noexcept {
            _cb(*this);
            delete this;
            return std::noop_coroutine();
        }
            
        futimpl(Fn &&fn): _cb(std::forward<Fn>(fn)) {
            this->_awaiter = resumable_handle_t(this);
        }
        virtual ~futimpl() = default;

    protected:
        Fn _cb;
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
        promise<T> p = f->get_promise();
        return p;
    } else {
        auto f = new(buffer) futimpl_inl(std::forward<Fn>(fn));
        promise<T> p = f->get_promise();
        return p;        
    }
}



}
#endif /* SRC_COCLASSES_FUTURE_H_ */
