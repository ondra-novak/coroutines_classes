#pragma once
#ifndef SRC_COCLASSES_FUTURE_H_
#define SRC_COCLASSES_FUTURE_H_

#include "common.h"
#include "resume_lock.h"
#include "exceptions.h"

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

    

    
    class abstract_awaiter {
    public:
        abstract_awaiter(Impl &owner):_owner(owner) {}
        virtual ~abstract_awaiter() = default;
        abstract_awaiter(const abstract_awaiter &) = default;
        abstract_awaiter &operator=(const abstract_awaiter &) = delete;
        virtual void resume() noexcept = 0;
        virtual std::coroutine_handle<> get_handle() {return {};};
        bool await_ready() const noexcept {
            return _owner._ready && _owner._pcount.load(std::memory_order_relaxed) == 0;
        }

    protected:
        Impl &_owner;
    };

    class co_awaiter: public abstract_awaiter {
    public:        
        co_awaiter(Impl &owner):abstract_awaiter(owner) {}
        
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            this->_owner.add_ref();
            this->_h = h;
            this->_owner._awaiter = this;
            this->_owner.release_ref();
            return resume_lock::await_suspend(h);
        }

        virtual std::coroutine_handle<> get_handle() override {
            return _h;
        };

        virtual void resume() noexcept override {
            resume_lock::resume(_h);            
        }
    protected:
        std::coroutine_handle<> _h;
    };

    class blocking_awaiter: public abstract_awaiter {
    public:
        blocking_awaiter(Impl &owner):abstract_awaiter(owner) {}
        void wait() {
            this->_owner.add_ref();
            this->_owner._awaiter = this;
            this->_owner.release_ref();            
            std::unique_lock _(mx);
            //scan await ready for now only
            
            //this is important as later we need to check our notification, not await_ready
            //because if await_ready awas false, we start wait, but we also promise
            //to keep this awaiter alive until notification arrives
            //so even if the await_ready becomes true, we still need to wait for a call of the resume()
            //to not release awaiter before the signal arrives.
            _signaled = this->await_ready();
            cvar.wait(_, [&]{return _signaled;});
        }
        virtual void resume() noexcept override {
            std::unique_lock _(mx);
            _signaled = true;
            cvar.notify_all();
        }
    protected:
        std::mutex mx;
        bool _signaled = false;
        std::condition_variable cvar;
    };


protected:    
    std::atomic<unsigned int> _pcount;
    std::atomic<bool> _ready;
    abstract_awaiter *_awaiter = nullptr;
    

    void add_ref() {
        _pcount.fetch_add(1, std::memory_order_relaxed);
    }
   
    void release_ref() {
        if (_pcount.fetch_sub(1, std::memory_order_release)-1 == 0) {            
            if (_awaiter) {
                if (!this->_ready.load(std::memory_order_acquire)) {
                    static_cast<Impl *>(this)->set_exception(std::make_exception_ptr(await_canceled_exception()));
                }
                _awaiter->resume();
            }
        }
    }
    bool release_ref(std::coroutine_handle<> &hout) {
        if (_pcount.fetch_sub(1, std::memory_order_release)-1 == 0) {
            if (_awaiter) {
                if (!this->_ready.load(std::memory_order_acquire)) {
                    static_cast<Impl *>(this)->set_exception(std::make_exception_ptr(await_canceled_exception()));
                }
                hout = _awaiter->get_handle();
                if (hout == nullptr) {
                    _awaiter->resume();
                } else {
                    return true;
                }
            }
        }
        return false;
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
    
    class co_awaiter: public super_t::co_awaiter {
    public:
        co_awaiter(future &owner):super_t::co_awaiter(owner) {}
        T &await_resume() {
            if (this->_owner._is_exception) std::rethrow_exception(this->_owner._exception);
            else return this->_owner._value;
        }
    };
    

    ///future can be awaited
    co_awaiter operator co_await() {
        return co_awaiter(*this);
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

    ///Get value - wait for result blocking
    T &wait() {
        if (this->is_ready()) return get();
        typename super_t::blocking_awaiter awaiter(*this);
        awaiter.wait();
        return get();
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

    class co_awaiter:public super_t::co_awaiter {
    public:
        co_awaiter(future &owner):super_t::co_awaiter(owner) {}

        void await_resume() {
            if (this->_owner._exception) std::rethrow_exception(this->_owner._exception);            
        }
    };
    
    co_awaiter operator co_await() {
        return co_awaiter(*this);
    }

    ///Check state of future, throws exception if there is such exceptional state
    void get() {
        if (!this->is_ready()) throw value_not_ready_exception();
        if (this->_exception) std::rethrow_exception(this->_exception);
    }

    ///Get value - wait for result blocking
    void wait() {
        if (this->is_ready()) return get();
        blocking_awaiter awaiter(*this);
        awaiter.wait();
        return get();
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
        if (_owner) _owner->release_ref();
        _owner = nullptr;
    }

    ///Release promise object, but don't resume coroutine, instead return coroutine handle
    /**
     * @param hout variable which receives the handle
     * @retval true success, handle retrieved
     * @retval false no coroutine waiting yet, no coroutine at all, released promise
     */
    bool release(std::coroutine_handle<> &hout) {
        if (_owner) {
            bool x = _owner->release_ref(hout);
            _owner = nullptr;
            return x;
        } else {
            return false;
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
 * @return promise<T> object 
 * 
 * @note callback is executed only after all instances of the promise are destroyed. This helps
 * to reduce side-effect which could potential happen during setting the value of the promise. So
 * execution is postponed. However ensure, that when promise is resolved, the promise instance 
 * is being destroyed as soon as possible
 */
template<typename T, typename Fn>
promise<T> make_promise(Fn &&fn) {
    
    class futimpl: public future<T> {
    public:

        class awaiter: public future<T>::blocking_awaiter {
        public:
            awaiter(future<T> &owner):future<T>::blocking_awaiter(owner) {}
            virtual void resume() noexcept override {
                static_cast<futimpl &>(this->_owner).resume();
            }
        };

        futimpl(Fn &&fn):_awt(*this), _cb(std::forward<Fn>(fn)) {
            this->_awaiter = &_awt;
        }
        virtual ~futimpl() = default;
        void resume() noexcept  {
            _cb(*this);
            delete this;
        }
    protected:
        awaiter _awt;
        Fn _cb;
    };
    
    auto f = new futimpl(std::forward<Fn>(fn));
    promise<T> p = f->get_promise();
    return p;
}


}
#endif /* SRC_COCLASSES_FUTURE_H_ */
