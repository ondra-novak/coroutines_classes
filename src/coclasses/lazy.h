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

    auto join() {
        start();
        return task<T>::join();
    }

};


template<typename T> class lazy_promise: public task_promise<T>         
{
public:
    lazy_promise():_started(false) {}
    
    std::suspend_always initial_suspend() noexcept {
        task_promise<T>::initial_suspend();
        return {};
    }
    
    lazy<T> get_return_object() {
        return lazy<T>(this);
    }
protected:
    std::atomic<bool> _started;

    friend class lazy<T>;
};;




}
#endif /* SRC_COCLASSES_LAZY_H_ */
