#pragma once
#ifndef SRC_COCLASSES_SRC_SIMPLE_TASK_H_
#define SRC_COCLASSES_SRC_SIMPLE_TASK_H_


#include "value_or_exception.h"
#include <coroutine>

namespace cocls {

///Simple task - minimal task implementation
/**
 * @tparam T type of return value
 *
 * currently not much useful, because compilers doesn't support coroutine elision.
 * Simple design can (maybe) enable this feature in future.
 *
 * Until this happen, you can use preallocated_task_awaiter
 */
template<typename T>
class simple_task {
public:


    class promise_type {
    public:

        std::suspend_never initial_suspend() noexcept {return {};}
        std::suspend_always final_suspend() noexcept {return {};}

        simple_task get_return_object() {
          return simple_task(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::atomic<void *> _awaiter = nullptr;
        value_or_exception<T> _val;

        void return_value(T &&val) {
            _val.set_value(val);
            make_ready();
        }

        void unhandled_exception() {
            _val.unhandled_exception();
            make_ready();
        }

        void make_ready() {
            void *a = _awaiter.exchange(std::coroutine_handle<promise_type>::from_promise(*this).address());
            if (a) {
                auto h = std::coroutine_handle<>::from_address(a);
                h.resume();
            }
        }

    };

    bool await_ready() {
        return _h.promise()._awaiter == _h.address();
    }
    bool await_suspend(std::coroutine_handle<> h) {
        void *p = nullptr;
        if (!_h.promise()._awaiter.compare_exchange_strong(p, h.address())) {
            return false;
        } else {
            return true;
        }
    }
    auto await_resume() {
        return _h.promise()._val.get_value();
    }


    simple_task(std::coroutine_handle<promise_type> h):_h(h) {}

    ~simple_task() {
        if (_h) _h.destroy();
    }

    simple_task(const simple_task &other) = delete;
    simple_task(simple_task &&other):_h(other._h) {other._h = nullptr;}
    simple_task &operator=(const simple_task &other) = delete;

protected:
    std::coroutine_handle<promise_type> _h;
};


}



#endif /* SRC_COCLASSES_SRC_SIMPLE_TASK_H_ */
