/*
 * future.h
 *
 *  Created on: 1. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_FUTURE_H_
#define SRC_COCLASSES_FUTURE_H_

#include "task.h"

#include <assert.h>
#include <memory>

namespace cocls {


template<typename T>
class future;


template<typename T>
class promise;


template<typename T, typename Impl>
class future_base {
public:
    
    future_base():_pcount(0),_ready(false), _cb_awaiter(nullptr), _awaiter(nullptr) {}
    ~future_base() {
        assert(_pcount == 0);
        cb_resume();
    }
    
    future_base(const future_base &) = delete;
    future_base &operator=(const future_base &) = delete;
    
    ///attach a callback which is called when future is resolved
    /** this allows to use future outside of coroutine 
     * You can attach multiple callbacks 
     * 
     * @param fn
     */
    template<typename FN>
    auto operator>>(FN &&fn) -> decltype(std::declval<FN>()(std::declval<Impl &>()), void()){
        if (_ready) {
            fn(static_cast<Impl &>(*this));
            return;
        }
        auto x = create_awaitable(std::forward<FN>(fn));
        while (!_cb_awaiter.compare_exchange_weak(x->_next, x, std::memory_order_relaxed));
        if (_ready) {
            cb_resume();
        }
    }
    
    bool is_ready() {
        return _ready.load(std::memory_order_acquire);
    }

    

protected:
    
    class awaiter {
    public:        
        awaiter(Impl &owner):_owner(owner) {}
        awaiter(const awaiter &) = default;
        awaiter &operator=(const awaiter &) = delete;
        
        bool await_ready() const noexcept {
            return _owner._ready && _owner._pcount.load(std::memory_order_relaxed) == 0;            
        }
        void await_suspend(std::coroutine_handle<> h) {
            _owner.add_ref();
            _h = h;
            _owner._awaiter = this;
            _owner.release_ref();
        }

        void resume() {
            _h.resume(); 
        }
    protected:
        std::coroutine_handle<> _h;
        Impl &_owner;
    };


    class cb_awaitable {
    public:        
        virtual void resume(Impl &impl) noexcept = 0;        
        virtual ~cb_awaitable() =default;
        cb_awaitable *_next = nullptr;
    };
    
    template<typename FN>
    static cb_awaitable *create_awaitable(FN &&fn) {
        class AW: public cb_awaitable {
        public:
            AW(FN &&fn):fn(std::forward<FN>(fn)) {}
            virtual void resume(Impl &impl) noexcept override  {
                fn(impl);
                delete this;
            }            
        protected:
            FN fn;
        };
        return new AW(std::forward<FN>(fn));
    }

protected:    
    std::atomic<unsigned int> _pcount;
    std::atomic<bool> _ready;
    std::atomic<cb_awaitable *> _cb_awaiter;
    awaiter *_awaiter = nullptr;
    

    void add_ref() {
        _pcount.fetch_add(1, std::memory_order_relaxed);
    }
   
    void release_ref() {
        if (_pcount.fetch_sub(1, std::memory_order_release)-1 == 0) {            
            awaiter *aw = _awaiter;
            cb_resume();
            if (aw) {
                if (!this->_ready.load(std::memory_order_acquire)) {
                    static_cast<Impl *>(this)->set_exception(std::make_exception_ptr(await_canceled_exception()));
                }
                aw->resume();
            }
        }
    }
    
    void cb_resume() {
        auto x = _cb_awaiter.exchange(nullptr);
        while (x) {
            auto y = x;
            x = x->_next;
            y->resume(static_cast<Impl &>(*this));
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
    void check() {
        if (!this->is_ready()) throw value_not_ready_exception();
        if (this->_exception) std::rethrow_exception(this->_exception);
        
    }
    
    promise<void> get_promise();

protected:
    
    
     std::exception_ptr _exception;
     

     friend class future_base<void, future<void> >;
     friend class promise<void>;

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
class promise {
public:
    promise(future<T> &owner):_owner(owner) {_owner.add_ref();}
    promise(const promise &other):_owner(other._owner) {_owner.add_ref();}
    promise &operator=(const promise &other) = delete;
    ~promise() {_owner.release_ref();}
    
    ///set value
    void set_value(T &&x) const {
        _owner.set_value(std::move(x));
    }
    
    ///set value
    void set_value(const T &x) const {
        _owner.set_value(x);
    }
    
    ///set exception
    void set_exception(std::exception_ptr &&e) const {
        _owner.set_exception(std::move(e));
    }
    
    ///capture current exception
    void unhandled_exception() const {
        _owner.set_exception(std::current_exception());
    }
    
    ///promise can be used as callback function
    void operator()(T &&x) {
        set_value(std::move(x));
    }
    ///promise can be used as callback function
    void operator()(const T &x) {
        set_value(x);
    }
    
protected:
    future<T> &_owner;
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
class promise<void> {
public:
    promise(future<void> &owner):_owner(owner) {_owner.add_ref();}
    promise(const promise &other):_owner(other._owner) {_owner.add_ref();}
    promise &operator=(const promise &other) = delete;
    ~promise() {_owner.release_ref();}

    ///makes future ready
    void set_value() {
        _owner.set_value();
    }
    ///sets exception
    void set_exception(std::exception_ptr &&e) {
        _owner.set_exception(std::move(e));
    }
    ///capture unhandled exception
    void unhandled_exception() {
        _owner.set_exception(std::current_exception());
    }
    ///
    void operator()() {
        set_value();
    }

protected:
    future<void> &_owner;
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
    
    class futimpl: public future<T>, public future<T>::cb_awaitable {
    public:
        futimpl(Fn &&fn):_cb(std::forward<Fn>(fn)) {
            this->_cb_awaiter = this;
        }
        virtual ~futimpl() = default;
        virtual void resume(future<T> &x) noexcept override {
            _cb(x);
            delete this;
        }
    protected:
        Fn _cb;
    };
    
    auto f = new futimpl(std::forward<Fn>(fn));
    promise<T> p = f->get_promise();
    return p;
}


}
#endif /* SRC_COCLASSES_FUTURE_H_ */
