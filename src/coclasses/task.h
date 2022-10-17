/**
 * @file task.h  - task type coroutine
 */

#pragma once
#ifndef SRC_COCLASSES_TASK_H_
#define SRC_COCLASSES_TASK_H_

#include "common.h" 
#include "exceptions.h"
#include "debug.h"
#include "abstract_awaiter.h"
#include "poolalloc.h"
#include "resume_ctl.h"

#include "value_or_exception.h"
#include <atomic>
#include <cassert>
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


template<typename T> class task{
public:
    using promise_type = task_promise<T>;

    ///You can create empty task, which can be initialized later by assign
    task():_promise(nullptr) {}
    ///task is internaly constructed from pointer to a promise  
    task(task_promise<T> *promise): _promise(promise) {
        _promise->add_ref();
    }
    ///destruction of task decreases reference
    ~task() {
        if (_promise) _promise->release_ref();
    }
    
    ///you can copy task, which just increases references
    task(const task &other):_promise(other._promise) {
        _promise->add_ref();
    }
    ///you can move task
    task(task &&other):_promise(other._promise) {
        other._promise = nullptr;
    }
    ///you can assign
    task &operator=(const task &other){
        if (this != &other) {
            if (_promise) _promise->release_ref();
            _promise = other._promise;
            _promise->add_ref();
        }
        return *this;
    }
    ///you can move-assign
    task &operator=(task &&other) {
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
     * awaiting the task causes suspend of current coroutine until
     * the awaited task is finished
     */
    co_awaiter<task_promise<T>,true> operator co_await() {
        return *_promise;
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

    auto join() {
        blocking_awaiter<task_promise<T>, true> aw(*_promise);
        return aw.wait();
    }
    
    
    ///Retrieve unique coroutine identifier
    coro_id get_id() {
        return std::coroutine_handle<promise_type>::from_promise(*_promise).address();
    }
    
    
    bool valid() const {
        return _promise!=nullptr;
    }
    
protected:
    task_promise<T> *_promise;
    
    
    task_promise<T> *get_promise() const {return _promise;}
};




template<typename Owner> class abstract_task_awaiter {
public:
    abstract_task_awaiter(Owner &owner):_owner(owner) {}
    abstract_task_awaiter(const abstract_task_awaiter &other):_owner(other._owner) {
        assert(next == nullptr);
    }
    abstract_task_awaiter&operator=(const abstract_task_awaiter &other) = delete;
    virtual ~abstract_task_awaiter() = default;

    virtual void resume() noexcept = 0 ;

    abstract_task_awaiter *get_next() {
        return this->next;
    }

    void push_to(std::atomic<abstract_task_awaiter<Owner> *> &list) {
        while (!list.compare_exchange_weak(next, this));
    }

protected:
    abstract_task_awaiter *next = nullptr;
    Owner &_owner;
};



template<typename Owner> class task_blocking_awaiter: public abstract_task_awaiter<Owner> {
public:
    using super_t = abstract_task_awaiter<Owner>;
    
    task_blocking_awaiter(Owner &owner): super_t(owner) {}

    void wait() {
        if (this->_owner.is_ready()) return;        
        this->_owner.register_awaiter(this);
        _signal = this->_owner.is_ready();
        std::unique_lock _(mx);
        cond.wait(_, [&]{return _signal;});
    }
    virtual void resume() noexcept override  {
        std::unique_lock _(mx);
        _signal = true;
        cond.notify_all();
    }

protected:
    bool _signal = false;
    std::mutex mx;
    std::condition_variable cond;
};


template<typename Impl> class task_coroutine_base {
public:
    
};

template<typename T> 
class task_promise_base: public coro_promise_base  {
public:

    task_promise_base():_ref_count(0) {}

    task_promise_base(const task_promise_base &) = delete;
    task_promise_base &operator=(const task_promise_base &) = delete;
    ~task_promise_base() {
        if (_ready == State::ready) {
            std::exception_ptr e = _value.get_exception();
            if (e) debug_reporter::get_instance()
                    .report_exception(e, typeid(task<T>));
        }
    }

    task_initial_suspender initial_suspend()  noexcept {
        ++_ref_count;
        return {};
    }
    task_final_suspender final_suspend() noexcept {
        return task_final_suspender(--_ref_count == 0);
    }
    
    void add_ref() {
        _ref_count++;
    }
    void release_ref() {
        if (--_ref_count == 0) {
            task_promise<T> &me = static_cast<task_promise<T>  &>(*this);
            auto h = std::coroutine_handle<task_promise<T> >::from_promise(me);
            h.destroy();
        }
    }


    using AW = abstract_awaiter<true>;
    
    enum class State {
        not_ready,
        ready,
        processed
    };
    
    value_or_exception<T> _value;
    std::atomic<State> _ready;
    std::atomic<AW *> _awaiter_chain;
    
    bool is_ready() const {
        return _ready != State::not_ready;
    }
    
    bool subscribe_awaiter(AW *aw) {
        aw->subscribe(_awaiter_chain);
        if (is_ready()) {
            aw->resume_chain(_awaiter_chain, aw);
            return false;
        } else {
            return true;
        }
    }
    
    auto get_result() {
        State s = State::ready;
        if (!_ready.compare_exchange_strong(s, State::processed)) {
            if (s == State::not_ready) throw value_not_ready_exception();
        }
        return _value.get_value();
    }
    
   
    
    
    void unhandled_exception() {
        _value.unhandled_exception();
        this->_ready = State::ready;
        AW::resume_chain(this->_awaiter_chain, nullptr);
    }
    
    std::atomic<unsigned int> _ref_count;
};

template<typename T> 
class task_promise: public task_promise_base<T> {
public:
    using AW = typename task_promise_base<T>::AW;
    template<typename X>
    auto return_value(X &&val)->decltype(std::declval<value_or_exception<T> >().set_value(val)) {
        this->_value.set_value(std::forward<X>(val));
        this->_ready = task_promise_base<T>::State::ready;
        AW::resume_chain(this->_awaiter_chain, nullptr);
    }
    task<T> get_return_object() {
        return task<T>(this);
    }
};

template<> 
class task_promise<void>: public task_promise_base<void> {
public:
    using AW = typename task_promise_base<void>::AW;
    auto return_void() {
        this->_value.set_value();
        this->_ready = State::ready;
        AW::resume_chain(this->_awaiter_chain, nullptr);
    }
    task<void> get_return_object() {
        return task<void>(this);
    }
};


}


#endif /* SRC_COCLASSES_TASK_H_ */
