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

namespace cocls {


template<typename T>
class future;


template<typename T>
class promise;


///Creates future variable for coroutine
/**
 * Future variable can be used in coroutine and can be awaited.
 * @tparam T type of variable. It can be void
 * 
 * variable cannot be moved or copied
 */
template<typename T>
class future {
public:

    future():_pcount(0),_ready(false), _awaiter(nullptr),_is_exception(false) {}
    ~future() {
        assert(_pcount == 0);
        if (_ready.load(std::memory_order_acquire)) {
            if (_is_exception) _exception.~exception_ptr();
            else _value.~T();
        }
    }
    future(const future &) = delete;
    future &operator=(const future &) = delete;

    class awaiter {
    public:
        awaiter(future &owner):_owner(owner) {}
        awaiter(const awaiter &) = default;
        awaiter &operator=(const awaiter &) = delete;
        
        bool await_ready() const noexcept {
            return _owner._ready && _owner._pcount;            
        }
        void await_suspend(std::coroutine_handle<> h) {
            _owner.add_ref();
            _h = h;
            _owner._awaiter = this;
            _owner.release_ref();
        }
        T &await_resume() {
            if (_owner._is_exception) std::rethrow_exception(_owner._exception);
            else return _owner._value;
        }
        
        void resume() {
            _h.resume();
        }

    protected:
        std::coroutine_handle<> _h;
        future &_owner;
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

protected:
     std::atomic<unsigned int> _pcount;
     std::atomic<bool> _ready;
     awaiter *_awaiter;
     bool _is_exception;
     union {
         T _value;
         std::exception_ptr _exception;
     };
     
     void add_ref() {
         _pcount.fetch_add(1, std::memory_order_relaxed);
     }
    
     void release_ref() {
         if (_pcount.fetch_sub(1, std::memory_order_release)-1 == 0) {
             if (_awaiter) {
                 if (!this->_ready.load(std::memory_order_acquire)) {
                     this->set_exception(std::make_exception_ptr(await_canceled_exception()));
                 }
                 _awaiter->resume();
             }
         }
     }
     
     void set_exception(std::exception_ptr &&e) {
         if (!_ready) {
             new(&_exception) std::exception_ptr(std::move(e));
             _is_exception = true;
             _ready.store(true, std::memory_order_release);
             
         }
     }
     
     template<typename X>
     void set_value(X &&value) {
         if (!_ready) {
             new(&_value) T(std::forward<X>(value));
             _is_exception = false;
             _ready.store(true, std::memory_order_release);
             
         }
     }

     friend class promise<T>;

    
};

///Creates future variable for coroutine - void specialization
/**
 * This future value has no exact value. But it still can be resolved and can capture exception
 */
template<>
class future<void> {
public:

    future():_pcount(0),_ready(false), _awaiter(nullptr),_exception(nullptr) {}
    ~future() {
        assert(_pcount == 0);
    }
    future(const future &) = delete;
    future &operator=(const future &) = delete;

    class awaiter {
    public:
        awaiter(future &owner):_owner(owner) {}
        awaiter(const awaiter &) = default;
        awaiter &operator=(const awaiter &) = delete;
        
        bool await_ready() const noexcept {
            return _owner._ready && _owner._pcount;            
        }
        void await_suspend(std::coroutine_handle<> h) {
            _owner.add_ref();
            _h = h;
            _owner._awaiter = this;
            _owner.release_ref();
        }
        void await_resume() {
            if (_owner._exception) std::rethrow_exception(_owner._exception);            
        }
        
        void resume() {
            _h.resume();
        }

    protected:
        std::coroutine_handle<> _h;
        future &_owner;
    };
    
    awaiter operator co_await() {
        return awaiter(*this);
    }

    
    promise<void> get_promise();

protected:
    
    
     std::atomic<unsigned int> _pcount;
     std::atomic<bool> _ready;
     awaiter *_awaiter;
     std::exception_ptr _exception;
     
     void add_ref() {
         _pcount.fetch_add(1, std::memory_order_relaxed);
     }
    
     void release_ref() {
         if (_pcount.fetch_sub(1, std::memory_order_release)-1 == 0) {
             if (_awaiter) {
                 if (!this->_ready.load(std::memory_order_acquire)) {
                     this->set_exception(std::make_exception_ptr(await_canceled_exception()));
                 }
                 _awaiter->resume();
             }
         }
     }
     
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

}
#endif /* SRC_COCLASSES_FUTURE_H_ */
