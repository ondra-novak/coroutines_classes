#pragma once
#ifndef SRC_COCLASSES_LAZY_H_
#define SRC_COCLASSES_LAZY_H_

#include "task.h"

namespace cocls {

template<typename T>
class lazy_promise;

template<typename T>
class lazy: public task<T> {
public:
    using promise_type = lazy_promise<T>;

    lazy() {};
    lazy(promise_type *p):task<T>(p) {}

    task_awaiter<task_promise<T> > operator co_await();

    void start();

    auto join();

};


template<typename T> class lazy_promise: public task_promise<T>         
{
public:
    lazy_promise():_started(false) {}
    
    std::suspend_always initial_suspend() const noexcept {return {};}
    
    lazy<T> get_return_object() {
        return lazy<T>(this);
    }
protected:
    std::atomic<bool> _started;

    friend class lazy<T>;
};;


template<typename T>
task_awaiter<task_promise<T> > lazy<T>::operator co_await() {
    start();
    return task_awaiter<task_promise<T> >(*(this->_promise));
}

template<typename T>
inline auto lazy<T>::join()  {
    start();
    return task<T>::join();

}



template<typename T>
inline void cocls::lazy<T>::start() {
    auto prom = static_cast<lazy_promise<T> *>(this->_promise);
    if (prom->_started.exchange(true, std::memory_order_relaxed) == false) {
        auto h = std::coroutine_handle<lazy_promise<T> >::from_promise(*prom);
        h.resume();
    }
}

}
#endif /* SRC_COCLASSES_LAZY_H_ */
