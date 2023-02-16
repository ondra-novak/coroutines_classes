/**
 * @file lazy.h
 */
#pragma once
#ifndef SRC_COCLASSES_LAZY_H_
#define SRC_COCLASSES_LAZY_H_

#include "task.h"

namespace cocls {

template<typename T = void, typename Policy = void>
class lazy_promise;
template<typename T = void, typename Policy = void>
class lazy;
///Lazy coroutine is coroutine, which is executed on first await
/**
 * @tparam T type of result
 */
template<typename T, typename Policy>
class lazy: public task<T, Policy> {
public:
    using promise_type = lazy_promise<T, Policy>;
    using promise_type_base = typename task<T, Policy>::promise_type_base;

    lazy() {};
    lazy(promise_type_base *h):task<T, Policy>(h) {}

    ///co_await the result
    auto operator co_await() {
        start();
        return task<T, Policy>::operator co_await();
    }

    ///start coroutine now.
    void start() {
        auto prom = static_cast<lazy_promise<T> *>(this->get_promise());
        if (prom->_started.exchange(true, std::memory_order_relaxed) == false) {
            auto h = std::coroutine_handle<lazy_promise<T> >::from_promise(*prom);
            prom->_policy.resume(h);
        }
    }

    template<typename resumption_policy>
    void start(resumption_policy policy) {
        auto prom = static_cast<lazy_promise<T> *>(this->get_promise());
        if (prom->_started.exchange(true, std::memory_order_relaxed) == false) {
            auto h = std::coroutine_handle<lazy_promise<T> >::from_promise(*prom);
            policy.resume(h);
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
        auto prom = static_cast<lazy_promise<T> *>(this->get_promise());
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
        auto prom = static_cast<lazy_promise<T> *>(this->get_promise());
        prom->cancel();
        start();
    }

    decltype(auto) join() {
        start();
        return task<T, Policy>::join();
    }

    template<typename awaiter_resumption_policy>
    decltype(auto) join (awaiter_resumption_policy policy) {
        start();
        return task<T, Policy>::join(policy);
    }

    template<typename awaiter_resumption_policy, typename start_resumption_policy>
    decltype(auto) join (awaiter_resumption_policy policy, start_resumption_policy start_policy) {
        start(start_policy);
        return task<T, Policy>::join(policy);
    }
protected:
    lazy(promise_type_base &h):task<T, Policy>(h) {}

    friend class lazy_promise<T, Policy>;


};

template<typename T>
class lazy<T, void>: public lazy<T, typename resumption_policy::unspecified<void>::policy > {
public:
    using lazy<T, typename resumption_policy::unspecified<void>::policy >::lazy;
};

template<typename T, typename Policy>
class lazy_promise: public task_promise<T, Policy>
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
        task_promise<T, Policy>::initial_suspend();
        return {this};
    }

    void cancel() {
        _canceled = true;
    }

    lazy<T> get_return_object() {
        return lazy<T>(*this);
    }
protected:
    std::atomic<bool> _started;
    bool _canceled = false;



    template<typename, typename> friend class lazy;

};;

template<typename T, typename P>
    struct is_task<lazy<T,P> > : std::integral_constant<bool, true> {};
template<typename T, typename P>
    struct task_result<lazy<T,P> > {using type = T;};



}
#endif /* SRC_COCLASSES_LAZY_H_ */
