#pragma once
#ifndef SRC_COCLASSES_TASK_H_
#define SRC_COCLASSES_TASK_H_

#include "common.h" 
#include "exceptions.h"

#include <atomic>
#include <coroutine>
#include <future>
#include <optional>

namespace cocls {



///Task object, it is returned from the coroutine
/**
 * @code
 * task<int> cofunction() {
 *      co_return 42;
 * }
 * @endcode
 * 
 * Task is actually a kind of a smart pointer which holds the
 * coroutine frame even if the coroutine is already finished. You 
 * can copy this object, or await this object. You can multiple await
 * at the time. Once the result is available, all waiting coroutines
 * are resumed
 */
template<typename T = void> class task;

///Coroutine promise object - part of coroutine frame
/** This object is never used directly, but it is essential object to support coroutines
 * 
 */
template<typename T> class task_promise;

///Awaiter class
/** Awaiters are helper objects created for co_await. They
 * handles resumption of a suspended coroutine
 */
template<typename Imp> class task_awaiter;


template<typename T> class task_base {
public:
    using promise_type = task_promise<T>;

    ///You can create empty task, which can be initialized later by assign
    task_base():_promise(nullptr) {}
    ///task is internaly constructed from pointer to a promise  
    task_base(task_promise<T> *promise): _promise(promise) {
        _promise->add_ref();
    }
    ///destruction of task decreases reference
    ~task_base() {
        if (_promise) _promise->release_ref();
    }
    
    ///you can copy task, which just increases references
    task_base(const task_base &other):_promise(other._promise) {
        _promise->add_ref();
    }
    ///you can move task
    task_base(task_base &&other):_promise(other._promise) {
        other._promise = nullptr;
    }
    ///you can assign
    task_base &operator=(const task_base &other){
        if (this != &other) {
            if (_promise) _promise->release_ref();
            _promise = other._promise;
            _promise->add_ref();
        }
        return *this;
    }
    ///you can move-assign
    task_base &operator=(task_base &&other) {
        if (this != &other) {
            if (_promise) _promise->release_ref();
            _promise = other._promise;
            other._promise = nullptr;        
        }
        return *this;
    }
    ///task can be awaited
    /**
     * @return awaiter
     * 
     * awaiting the task causes suspend of curreny coroutine until
     * the awaited task is finished
     */
    task_awaiter<task_promise<T> > operator co_await() {
        return task_awaiter<task_promise<T> >(*_promise);
    }
    
    ///Determines whether task is finished and return value is ready
    /**
     * The main purpose of this function is to avoid calling costly coroutine when the
     * task is already finished.  
     * 
     * @retval true task is finished and return value is ready
     * @retval false task is not finished yet
     */
    bool is_ready() const {
        return _promise->is_ready();
    }

protected:
    task_promise<T> *_promise;
    
};


template<typename T>
class task: public task_base<T> {
public:
    task() = default;
    task(task_promise<T> *p):task_base<T>(p) {}

    ///try to retrieve return value of the task
    /**
     * @return return value of the task
     * @exception value_not_ready_exception when value is not ready yet
     */ 
    T &get() const {
        return this->_promise->get_value();
    }
    
    ///Retrieve result of the task - waits if task is not ready yet
    /**     
     * @return result of the task. Note that operation can be blocking. If the
     * task cannot be finished because this thread is blocked then deadlock can happen 
     */
    T wait() const {
        if (this->is_ready()) return this->get();
        else return this->as_future().get();
    }   

    std::future<T> as_future() const;
    
};

template<>
class task<void>: public task_base<void> {
public:
    task() = default;
    task(task_promise<void> *p):task_base<void>(p) {}
    ///Retrieve result of the task - waits if task is not ready yet
    /**     
     * @return result of the task. Note that operation can be blocking. If the
     * task cannot be finished because this thread is blocked then deadlock can happen 
     */
    void wait() const {
        if (!is_ready()) {
            return as_future().wait();
        }
    }   
    
    std::future<void> as_future() const;
};




template<typename Owner> class task_awaiter {
public:
    using Value = typename Owner::ValueT;
    using ValRef = Reference<Value>;
    
    task_awaiter(Owner &owner): _owner(owner) {}
    
    task_awaiter(const task_awaiter &other) = delete;
    task_awaiter &operator=(const task_awaiter &other) = delete;
    
    bool await_ready() const noexcept {
        return _owner.is_ready();
    }
    bool await_suspend(std::coroutine_handle<> h) {
        _h = h;
        return _owner.register_awaiter(this);
    }
    ValRef await_resume() {
        return _owner.get_value();
    }
    void resume() {
        _h.resume();
    }

    void push_to(std::atomic<task_awaiter<Owner> *> &list) {
        while (!list.compare_exchange_weak(next, this));
    }
    
    task_awaiter *get_next() {
        return next;
    }
protected:
    task_awaiter *next = nullptr;
    Owner &_owner;
    std::coroutine_handle<> _h;
    

};

template<typename Impl> class task_promise_base {
public:

    
    task_promise_base():_ready(false),_awaiters(nullptr) {}

    void resolve(task_awaiter<Impl> *dont_resume) {
        task_awaiter<Impl> *list = _awaiters.exchange(nullptr);
        while (list) {
            auto *p = list;
            list = list->get_next();
            if (p != dont_resume) p->resume();            
        }
    }
    
    bool is_ready() const {
        return _ready;
    }

    bool register_awaiter(task_awaiter<Impl> *ptr) {
        if (_ready) return false;
        ptr->push_to(_awaiters);
        if (_ready) {
            resolve(ptr);
            return false;
        }
        return true;
    }
    
    auto &get_value() {
        return static_cast<const Impl &>(*this).get_value();
    }
    
    
protected:
    std::atomic<bool> _ready;
    std::atomic<task_awaiter<Impl> *> _awaiters;
};

template<typename Impl> class task_coroutine_base {
public:
    task_coroutine_base():_ref_count(1) {}

    task_coroutine_base(const task_coroutine_base &) = delete;
    task_coroutine_base &operator=(const task_coroutine_base &) = delete;

    ///handles final_suspend
    class final_awaiter {
    public:
        final_awaiter(task_coroutine_base &prom): _owner(prom) {}        
        
        final_awaiter(const final_awaiter &prom) = default;
        final_awaiter &operator=(const final_awaiter &prom) = delete;
        
        bool await_ready() const noexcept {
            return _owner._ref_count == 0;
        }
        constexpr void await_suspend(std::coroutine_handle<>) const noexcept {}
        constexpr void await_resume() const noexcept {}        
    protected:
        task_coroutine_base &_owner;
    };
    
    std::suspend_never initial_suspend() const noexcept {return {};}
    final_awaiter final_suspend() noexcept {
        --_ref_count;
        return *this;
    }
    
    void add_ref() {
        _ref_count++;
    }
    void release_ref() {
        if (--_ref_count == 0) {
            Impl &me = static_cast<Impl &>(*this);
            auto h = std::coroutine_handle<Impl >::from_promise(me);
            h.destroy();
        }
    }

    
protected:
    std::atomic<unsigned int> _ref_count;
};

template<typename T> class task_promise:
        public task_coroutine_base<task_promise<T> >,
        public task_promise_base<task_promise<T> >
        
{
public:
    
    using ValueT = T;
    using awaiter = task_awaiter<task_promise<T> >;
     
    
    task_promise(): _is_exception(false)  {}
    ~task_promise() {
        if (this->is_ready()) {
            if (_is_exception) {
                _exception.~exception_ptr();
            } else {
                _value.~T();
            }
        }
    }

    typename task_coroutine_base<task_promise<T> >::final_awaiter final_suspend() noexcept {
        this->resolve(nullptr);
        return task_coroutine_base<task_promise<T> >::final_suspend();
    }

    
    void return_value(T &&val) {
        if (!this->_ready) {
            new(&_value) T(std::move(val));
            this->_ready = true;
        }
    }

    void return_value(const T &val) {
        if (!this->_ready) {
            new(&_value) T(val);
            this->_ready = true;
        }
    }

    void unhandled_exception() {
        set_exception(std::current_exception());
    }
    
    void set_exception(std::exception_ptr &&e) {
        if (!this->_ready) {
            new(&_exception) std::exception_ptr(e);
            _is_exception = true;
            this->_ready = true;
        }        
    }
    
    T &get_value()  {
        if (!this->_ready) {
            throw value_not_ready_exception();
        }
        if (_is_exception) {
            std::rethrow_exception(_exception);
        } else {
            return _value;
        }
    }
        
    
    
    task<T> get_return_object() {
        return task<T>(this);
    }
    
    
protected:
    bool _is_exception;
        
    union {
        T _value;
        std::exception_ptr _exception;
    };
};

template<> class task_promise<void>: 
        public task_promise_base<task_promise<void> >,
        public task_coroutine_base<task_promise<void> >
{
public:
    using ValueT = void;
    
    typename task_coroutine_base<task_promise<void> >::final_awaiter final_suspend() noexcept {
        this->resolve(nullptr);
        return task_coroutine_base<task_promise<void> >::final_suspend();
    }

    
    void unhandled_exception() {
        set_exception(std::current_exception());
    }
    
    void set_exception(std::exception_ptr &&e) {
        if (!this->_ready) {
            _exception = std::move(e);
        }        
    }
    
    void get_value()  {
        if (!this->_ready) {
            throw value_not_ready_exception();
        }
        if (_exception) {
            std::rethrow_exception(_exception);
        }
    }
        
    
    void return_void() {
        this->_ready = true;
    }
    
    task<void> get_return_object() {
        return task<void>(this);
    }
    
    
protected:
    std::exception_ptr _exception;
};

template<typename T>
std::future<T> task<T>::as_future() const {
    std::future<T> f;
    ([&]()->task<>{
       std::promise<T> p;
       task me = *this;
       f = p.get_future();
       p.set_value(co_await me);
    })();
    return f;
}



std::future<void> task<void>::as_future() const {
       std::future<void> f;
       ([&]()->task<>{
          std::promise<void> p;
          task me = *this;
          f = p.get_future();
          co_await me;
          p.set_value();
       })();
       return f;
       
}


}


#endif /* SRC_COCLASSES_TASK_H_ */
