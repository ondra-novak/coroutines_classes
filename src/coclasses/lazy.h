/**
 * @file lazy.h
 */
#pragma once
#ifndef SRC_COCLASSES_LAZY_H_
#define SRC_COCLASSES_LAZY_H_

#include "task.h"

namespace cocls {

template<typename T = void>
class lazy_promise;

///Lazy coroutine is coroutine, which is executed on first await
/**
 * @tparam T type of result
 */
template<typename T>
class lazy: public task<T> {
public:
    using promise_type = lazy_promise<T>;

    lazy() {};
    lazy(promise_type *p):task<T>(p) {}

    ///co_await the result
    co_awaiter<task_promise<T>,true> operator co_await() {
        start();
        return co_awaiter<task_promise<T>, true >(*(this->_promise));
    }

    ///start coroutine now.
    void start() {
        auto prom = static_cast<lazy_promise<T> *>(this->_promise);
        if (prom->_started.exchange(true, std::memory_order_relaxed) == false) {
            auto h = std::coroutine_handle<lazy_promise<T> >::from_promise(*prom);
            resume_ctl::resume(h);
        }
    }

    ///Retrieve handle which is used to start this coroutine
    /**
     * @return handle to start this coroutine. If the coroutine is already running, returns
     * nullptr. Always check the return value! 
     * 
     * @note From the perspective of outside work, this function marks coroutine running even
     * if it is not running yet. It is expected, that owner of the handle resumes the coroutine.
     * Coroutine in this state can be still canceled. This is the only way to cancel execution. 
     * Do not destroy handle, because it still can be awaited
     * 
     */
    [[nodiscard]] std::coroutine_handle<> get_start_handle() {
        auto prom = static_cast<lazy_promise<T> *>(this->_promise);
        if (prom->_started.exchange(true, std::memory_order_relaxed) == false) {
            auto h = std::coroutine_handle<lazy_promise<T> >::from_promise(*prom);
            return h;
        } else {
            return nullptr;
        }
    }
  
    
    ///Marks lazy task as canceled.
    /**
     * This allows to cancel lazy task before it is started. If the task is already running,
     * function does nothing as the task cannot be canceled now.
     * 
     * Function doesn't resume the task. You need to co_await or start() the task. If you 
     * also have a handle of the task, you need to resume the handle.
     * 
     * 
     */
    void mark_canceled() {
        auto prom = static_cast<lazy_promise<T> *>(this->_promise);
        prom->cancel();
        start();
    }

    auto join() {
        start();
        return task<T>::join();
    }

};


template<typename T> class lazy_promise: public task_promise<T>         
{
public:
    lazy_promise():_started(false) {}
    
    struct initial_awaiter {
        lazy_promise *_owner;
        initial_awaiter(lazy_promise *owner):_owner(owner) {}
        initial_awaiter(const initial_awaiter &) = delete;
        initial_awaiter &operator=(const initial_awaiter &) = delete;
        
        static bool await_ready() noexcept {return false;}
        static void await_suspend(std::coroutine_handle<> ) noexcept {};
        void await_resume() {
            if (_owner->_canceled) throw await_canceled_exception();
        }
    };
    
    initial_awaiter initial_suspend() noexcept {
        task_promise<T>::initial_suspend();
        return {this};
    }
    
    void cancel() {
        _canceled = true;
    }
    
    lazy<T> get_return_object() {
        return lazy<T>(this);
    }
protected:
    std::atomic<bool> _started;
    bool _canceled = false;

    friend class lazy<T>;
};;




}
#endif /* SRC_COCLASSES_LAZY_H_ */
