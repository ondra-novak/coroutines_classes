/**
 * @file task.h  - task type coroutine
 */

#pragma once
#ifndef SRC_COCLASSES_TASK_H_
#define SRC_COCLASSES_TASK_H_

#include "common.h" 
#include "exceptions.h"
#include "abstract_awaiter.h"
#include "debug.h"
#include "poolalloc.h"
#include "resumption_policy.h"

#include "value_or_exception.h"
#include <atomic>
#include <cassert>
#include <coroutine>
#include <future>
#include <optional>
#include <type_traits>



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
 * 
 * @tparam Policy specifies resumption policy for this coroutine. This policy
 * is used for initial resuption and for all co_await when the awaiter supports
 * function set_resumption_policy(). 
 * 
 * If void specified, then default_resumption_policy is used. You can specialize
 * this template to void to change own default_resumption_polici
 */
template<typename T = void, typename Policy = void> class task;

///Coroutine promise object - part of coroutine frame
/** This object is never used directly, but it is essential object to support coroutines
 * 
 */
template<typename T, typename Policy> class task_promise;

template<typename T> class task_promise_base;


template<typename T, typename Policy> class task{
public:
    using promise_type = task_promise<T, Policy>;
    using promise_type_base = task_promise_base<T>;

    ///You can create empty task, which can be initialized later by assign
    task():_promise(nullptr) {}
    ///task is internaly constructed from pointer to a promise  
    task(promise_type_base *promise): _promise(promise) {
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
     *     
     */
    co_awaiter<promise_type_base, Policy, true> operator co_await() {
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

    ///Join the task synchronously, returns value
    auto join() {
        blocking_awaiter<promise_type_base, true> aw(*_promise);
        return aw.wait();
    }
    
    ///Join the task asynchronously with specified resumption policy
    /**
     * @param policy instance of resumption policy
     * @return awaiter
     *
     * @code
     *   //resume this coroutine in specified thread pool once the task is ready
     * int result = co_await task.join(thread_pool_resumption_policy(pool));
     *
     *
     */
    template<typename resumption_policy>
    co_awaiter<promise_type, resumption_policy, true> join(resumption_policy policy) {
        return co_awaiter<promise_type, resumption_policy, true>(*_promise, std::forward<resumption_policy>(policy));
    }

    
    ///Retrieve unique coroutine identifier
    coro_id get_id() const {
        return std::coroutine_handle<promise_type_base>::from_promise(*_promise).address();
    }
    
    
    bool valid() const {
        return _promise!=nullptr;
    }
    
    
    template<typename _Policy>
    operator task<T, _Policy>() {
        return task<T, _Policy>(_promise);
    }

    template<typename ... Args>
    void initialize_policy(Args && ... args) {
        static_cast<promise_type *>(_promise)->initialize_policy(std::forward<Args>(args)...);
    }
    
protected:
    promise_type_base *_promise;
    
    
    promise_type_base *get_promise() const {return _promise;}
};

namespace _details {
    
    template<typename X>
    auto test_has_co_await(X &&x) -> decltype(x.operator co_await());
    std::monostate test_has_co_await(...);
    template<typename X> 
    using has_co_await = std::negation<std::is_same<std::monostate, decltype(test_has_co_await(std::declval<X>()))> >;

    template<typename X, typename Y>
    auto test_has_set_resumption_policy(X &&x, Y &&y) -> decltype(x.set_resumption_policy(std::forward<Y>(y)));
    std::monostate test_has_set_resumption_policy(...);
    template<typename X, typename Y> 
    using has_set_resumption_policy = std::negation<std::is_same<std::monostate, decltype(test_has_set_resumption_policy(std::declval<X>(), std::declval<Y>()))> >;

}


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

    ///handles final_suspend
    class final_awaiter {
    public:
        final_awaiter(task_promise_base &prom): _owner(prom) {}        
        
        final_awaiter(const final_awaiter &prom) = default;
        final_awaiter &operator=(const final_awaiter &prom) = delete;
        
        bool await_ready() noexcept {
            return _owner._ref_count == 0;
        }
        static void await_suspend(std::coroutine_handle<>) noexcept {}
        constexpr void await_resume() const noexcept {}        
    protected:
        task_promise_base &_owner;
    };
    
    final_awaiter final_suspend() noexcept {
        --_ref_count;        
        return *this;
    }
    
    void add_ref() {
        _ref_count++;
    }
    void release_ref() {
        if (--_ref_count == 0) {
            auto h = std::coroutine_handle<task_promise_base>::from_promise(*this);
            h.destroy();
        }
    }

    /*
    template<typename X>
    auto await_transform(X&& awt) noexcept
        -> resume_ctl_awaiter<decltype(std::declval<X>().operator co_await())> {
        return resume_ctl_awaiter<decltype(std::declval<X>().operator co_await())>(
                awt.operator co_await()
        );
    }
    
    template<typename X>
    auto await_transform(X&& awt) noexcept
        -> decltype(awt.await_resume(),resume_ctl_awaiter<X>(std::forward<X>(awt))) {
        return resume_ctl_awaiter<X>(std::forward<X>(awt));
    }
*/
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


template<typename T, typename Policy>
class task_promise_with_policy: public task_promise_base<T> {
public:

    template<typename Awt>
    auto await_transform(Awt&& awt) noexcept {
        if constexpr (_details::has_co_await<Awt>::value) {
            return await_transform(awt.operator co_await());
        } else if constexpr (_details::has_set_resumption_policy<Awt, Policy>::value) {
            return awt.set_resumption_policy(Policy());
        } else {
            return std::forward<Awt>(awt);
        }
    }

    class initial_awaiter {
    public:
        initial_awaiter(Policy &p):_p(p) {}
        initial_awaiter(const initial_awaiter &) = default;
        initial_awaiter &operator=(const initial_awaiter &) = delete;
        
        static bool await_ready() noexcept {return false;}
        void await_suspend(std::coroutine_handle<> h) noexcept {
            _p.resume(h);
        }
        static void await_resume() noexcept {}
    protected:
        Policy &_p;
    };
    
    initial_awaiter initial_suspend()  noexcept {
        ++this->_ref_count;
        return {_policy};
    }

    template<typename ... Args>
    void initialize_policy(Args &&... args) {
        _policy.initialize_policy(std::forward<Args>(args)...);
    }

    Policy _policy;
};

template<typename T>
class task_promise_with_policy<T, void>: public task_promise_with_policy<T, default_resumption_policy<void> > {
};

template<typename T, typename Policy>
class task_promise: public task_promise_with_policy<T, Policy> {
public:
    using AW = typename task_promise_with_policy<T, Policy>::AW;
    template<typename X>
    auto return_value(X &&val)->decltype(std::declval<value_or_exception<T> >().set_value(val)) {
        this->_value.set_value(std::forward<X>(val));
        this->_ready = task_promise_base<T>::State::ready;
        AW::resume_chain(this->_awaiter_chain, nullptr);
    }
    task<T, Policy> get_return_object() {
        return task<T, Policy>(this);
    }
};

template<typename Policy>
class task_promise<void, Policy>: public task_promise_with_policy<void, Policy> {
public:
    using AW = typename task_promise_with_policy<void, Policy>::AW;
    auto return_void() {
        this->_value.set_value();
        this->_ready = task_promise_base<void>::State::ready;
        AW::resume_chain(this->_awaiter_chain, nullptr);
    }
    task<void, Policy> get_return_object() {
        return task<void, Policy>(this);
    }
};


}


#endif /* SRC_COCLASSES_TASK_H_ */
