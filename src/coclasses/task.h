/**
 * @file task.h  - task type coroutine
 */

#pragma once
#ifndef SRC_COCLASSES_TASK_H_
#define SRC_COCLASSES_TASK_H_

#include "awaiter.h"
#include "common.h"
#include "debug.h"
#include "exceptions.h"
#include "resumption_policy.h"
#include "poolalloc.h"

#include "coro_policy_holder.h"

#include <atomic>
#include <coroutine>
#include <cassert>
#include <cstddef>
#include <exception>
#include <new>
#include <type_traits>

namespace cocls {

///Coroutine promise object - part of coroutine frame
/** This object is never used directly, but it is essential object to support coroutines
 *
 */
template<typename T, typename Policy> class task_promise;

class task_promise_base;
template<typename T> class task_promise_value;


template<std::size_t space> class static_storage;

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
 * function resumption_policy::_awaiter_concept::set_resumption_policy().
 *
 * If void specified, then default_resumption_policy is used. You can specialize
 * this template to void to change own default_resumption_polici
 *
 * @see resumption_policy
 *
 */
template<typename T = void, typename Policy = void>
class task {
public:

    using promise_type = task_promise<T, Policy>;
    using promise_type_base = task_promise_value<T>;
    using value_type = T;

    ///You can create empty task, which can be initialized later by assign
    /** For the task<void>, the object is already initialized and co_await on such task
     * is always resolved
     */
    task() = default;

    ///Create task which has already result
    /**
     * Function is useful when result is already known. F
     * @param args arguments are used to construct the result
     * @return finished task
     */
    template<typename ... Args>
    static task set_result(Args && ... args);

    ///Create task which has already result - in exception state
    /**
     * Function captures current exception
     * @return finished task
     */
    static task set_exception();

    ///Create task which has already result - in exception state
    /**
     * @param e exception
     * Function captures current exception
     * @return finished task
     */
    static task set_exception(std::exception_ptr e);


    ///task is internaly constructed from pointer to a promise
    task(promise_type_base *p): _promise(p) {
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
        other._promise= nullptr;
    }
    ///you can assign
    task &operator=(const task &other){
        if (this != &other) {
            if (_promise) _promise->release_ref();
            _promise= other._promise;
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
    co_awaiter<promise_type_base> operator co_await() {
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

    ///Determines whether task is finished and return value is ready
    /**
     * The main purpose of this function is to avoid calling costly coroutine when the
     * task is already finished.
     *
     * @retval true task is finished and return value is ready
     * @retval false task is not finished yet
     */
    bool done() const {
        return is_ready();
    }

    ///Retrieve return value of the task
    /**
     * @return return value
     * @exception value_not_ready_exception requested value while task is still pending
     *
     * @note Useful only when task is already finished. Otherwise use co_await or join()
     */
    std::add_lvalue_reference_t<T> value() {
        return _promise->get_result();
    }

    ///Retrieve return value of the task
    std::add_const_t<std::add_lvalue_reference_t<T> > value() const {
        return _promise->get_result();
    }

    ///Join the task synchronously, returns value
    decltype(auto) join() {
        co_awaiter<promise_type_base> aw(*_promise);
        return aw.wait();
    }


    ///Join the task synchronously, but do not pick result
    /**
     * Allows to synchronize code, waiting for task to finish, but
     * doesn't pick any result nor exception.
     */
    void sync() noexcept {
        co_awaiter<promise_type_base> aw(*_promise);
        aw.sync();
    }


    ///Retrieve unique coroutine identifier
    coro_id get_id() const {
        return _promise;
    }


    ///Determines whether object contains a valid task
    bool valid() const {
        return _promise!=nullptr;
    }

    ///Allows to convert this object to object with unspecified policy
    /**
     * This helps to work with task<> objects, where policy has no meaning
     *
     * Calling initialize_policy() after conversion is UB
     */
    operator task<T>() {
        return task<T>(_promise);
    }

    ///Initializes resumption policy
    /**
     * For resumption policies with arguments, this allows to pass arguments to
     * the policy associated with the task. Such policies can't execute the task
     * until the policy is initialized.
     *
     * @param args arguments passed to the policy
     */
    template<typename ... Args>
    void initialize_policy(Args && ... args) {
        auto prom = static_cast<promise_type *>(_promise);
        if (prom->initialize_policy(std::forward<Args>(args)...)) {
            prom->_policy.resume(std::coroutine_handle<promise_type>::from_promise(*prom));
        }
    }



protected:

    task(promise_type_base &promise):_promise(&promise) {}

    friend class task_promise<T, Policy>;

    template<typename A, typename B>
    friend class task;

    promise_type_base *_promise = nullptr;

    promise_type_base * get_promise() const {return _promise;}

    template<typename X, typename ... Args>
    static decltype(auto) get_first(X &&x, Args && ...) {
         return std::forward<X>(x);
    }


};



class task_promise_base: public coro_promise_base  {
public:
    using AW = abstract_awaiter;

    //contains list of awaiters - linked list (this->_next)
    std::atomic<AW *> _awaiter_chain;
    //contains status and counter
    /* format DEPCCC..CCCCC
     *
     * - D - data ready
     * - E - exception ready
     * - P - results processed (get_result() called)
     * - C - ref counter;
     *
     * See masks below
     *
     */
    std::atomic<std::size_t> _status_ref_count;
    //Contains handle of this coroutine
    /* We can't use promise address to retrieve handle, because in some cases,
     * not whole class is used (especially when policy is stripped). While the
     * promise pointer is still valid (because it points to base class), function
     * from_promise() calculates offset of the frame from the given class, which
     * which cannot be the base class, but the final class. To avoid using of
     * virtual functions, the handle is received and stored during final_suspend(),
     * becuase we only need the handle to destroy coroutine, otherwise this field is empyu
     */
    std::coroutine_handle<> _my_handle;

    //masks "data available" bit
    static constexpr std::size_t data_mask = (static_cast<std::size_t>(1)<<(sizeof(std::size_t)*8-1));
    //masks "exception available" bit
    static constexpr std::size_t except_mask = (static_cast<std::size_t>(1)<<(sizeof(std::size_t)*8-2));
    //masks "processed" bit
    static constexpr std::size_t processed_mask = (static_cast<std::size_t>(1)<<(sizeof(std::size_t)*8-3));
    //combination of all three bits from above - check for "data_ready"
    static constexpr std::size_t ready_mask = data_mask | except_mask | processed_mask;
    //masks the counter
    static constexpr std::size_t counter_mask = static_cast<std::size_t>(-1) ^ ready_mask;

    //task promise is always ref/counted +1 during its execution
    //and additional +1 because get_return_object doesn't increase ref_count
    //and this saves one extra atomic increment
    task_promise_base():_status_ref_count(2) {}

    //task promise can't be copied
    task_promise_base(const task_promise_base &) = delete;
    //task promise can't be assigned
    task_promise_base &operator=(const task_promise_base &) = delete;

    //destroys coroutine - this is possible after it is finished
    void destroy() {
        assert("Attempt to destroy a running task. Missing co_await <task>?" && !!_my_handle);
        _my_handle.destroy();
    }

    //handles final_suspend
    class final_awaiter: public std::suspend_always {
    public:
        //record ownership
        final_awaiter(task_promise_base &prom): _owner(prom) {}
        //can't be copied
        final_awaiter(const final_awaiter &prom) = default;
        //can't be assigned
        final_awaiter &operator=(const final_awaiter &prom) = delete;

        //awaiting coroutine is resumed during this suspend
        //using symmetric transfer
        //this speeds up returning from coroutine to coroutine
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> myhandle) noexcept {

            auto noop = std::noop_coroutine();
            std::coroutine_handle<> ret = noop;
            //get list of awaiters
            auto awt = _owner._awaiter_chain.exchange(&empty_awaiter::disabled);
            while (awt) {
                auto x = awt;
                awt = awt->_next;
                //cycle until at least one awaiter returns valid coroutine handle
                //from resume_handle()
                //some awaiters are not coroutines, they perform its own
                //resume operation and returns noop_coroutine()

                auto h = x->resume_handle();
                //if h is not noop, we found coroutine, which will be resumed at current thread
                if (h != noop) {
                    //cycle to rest of awaiters
                    while (awt) {
                        x = awt;
                        awt = awt->_next;
                        //resume them as usual
                        x->resume();
                    }
                    //perform symmetric transfer to chosen coroutine
                    ret = h;
                }
            }

            //decrease reference count and check result
            if (((--_owner._status_ref_count) & counter_mask) == 0) {
                //If the result is 0, we need destroy itself
                myhandle.destroy();
            } else {
                //otherwise task will be destroyed by owner.
                //store handle to the promise object.
                _owner._my_handle = myhandle;
            }
            //exit to the previous stack frame
            return ret;
        }
    protected:
        task_promise_base &_owner;
    };

    //retrieve final awaiter
    final_awaiter final_suspend() noexcept {
        return *this;
    }

    //+1 ref count
    void add_ref() {
        _status_ref_count.fetch_add(1,std::memory_order_relaxed);
    }
    //-1 ref count
    void release_ref() {
        if (((_status_ref_count.fetch_sub(1, std::memory_order_release) -1 ) & counter_mask) == 0) {
            destroy();
        }
    }

    //adds awaiter to the chain
    bool subscribe_awaiter(AW *aw) {
        return aw->subscribe_check_ready(_awaiter_chain, empty_awaiter::disabled);
    }

    auto set_ready_data() {
        return _status_ref_count.fetch_or(data_mask);
    }

    auto set_ready_exception() {
        return _status_ref_count.fetch_or(except_mask);
    }

    auto set_processed() {
        return _status_ref_count.fetch_or(processed_mask);
    }

    bool is_ready() const {
        return (_status_ref_count.load(std::memory_order_relaxed) & ready_mask) != 0;
    }



};

template<typename T>
class task_promise_value: public task_promise_base {
public:
    using super = task_promise_base;

    task_promise_value() {}

    union {
        T _v;
        std::exception_ptr _e;
    };

    using AW = typename task_promise_base::AW;
    template<typename X>
    auto return_value(X && val)->std::enable_if_t<std::is_convertible_v<X,T> >{
        new(&_v) T(std::forward<X>(val));
        this->set_ready_data();
    }
    T &get_result() {
        auto status = this->set_processed();
        if (status & this->data_mask) {
            return _v;
        }
        if (status & this->except_mask) {
            std::rethrow_exception(_e);
        }
        throw value_not_ready_exception();
    }

    template<typename ... X>
    void emplace(X && ... x) {
        new(&_v) T(std::forward<X>(x)...);
        this->set_ready_data();
    }


    void unhandled_exception() {
        new(&_e) std::exception_ptr(std::current_exception());
        this->set_ready_exception();
    }

    ~task_promise_value() {
        auto status = this->_status_ref_count.load(std::memory_order_acquire);
        switch (status & this->ready_mask) {
            case super::data_mask:
            case super::data_mask | super::processed_mask:
                _v.~T();
                break;
            case super::except_mask:
                debug_reporter::current_instance->report_exception(_e, typeid(task<T>));
                [[fallthrough]];
            case super::except_mask | super::processed_mask:
                _e.~exception_ptr();
                break;
            default:
                break;

        }
    }

};

template<>
class task_promise_value<void>: public task_promise_base {
public:

    std::exception_ptr _e;

    using AW = typename task_promise_base::AW;
    auto return_void() {
        this->set_ready_data();
    }
    void unhandled_exception() {
        _e =  std::current_exception();
        this->set_ready_exception();
    }
    void get_result()  {
        auto status = this->set_processed();
        if (status & this->data_mask) {
            return;
        }
        if (status & this->except_mask) {
            std::rethrow_exception(_e);
        }
        throw value_not_ready_exception();
    }

    ~task_promise_value() {
        auto status = this->_status_ref_count.load(std::memory_order_acquire);
        if ((status & this->ready_mask) == this->except_mask) {
                debug_reporter::current_instance->report_exception(_e, typeid(task<void>));
        }
    }
};




template<typename T, typename Policy>
class task_promise: public task_promise_value<T>, public coro_policy_holder<Policy> {
public:


    auto get_return_object() {
        return task<T, Policy>(*this);
    }
};




template<typename T>
    struct is_task :std::integral_constant<bool, false> {};
template<typename T, typename P>
    struct is_task<task<T,P> > : std::integral_constant<bool, true> {};
template<typename T>
    struct task_result;
template<typename T, typename P>
    struct task_result<task<T,P> > {using type = T;};

namespace _details {
    struct BoolInit{
        task<bool, resumption_policy::immediate> res_true;
        task<bool, resumption_policy::immediate> res_false;
        static task<bool, resumption_policy::immediate> coro(bool b) {
            co_return b;
        }
        BoolInit(): res_true(coro(true))
                  , res_false(coro(false)) {}
    };
}



template<typename T, typename Policy>
template<typename ... Args>
inline task<T, Policy> task<T, Policy>::set_result(Args &&... args) {
    if constexpr (std::is_void_v<T>) {
        auto coro = [&]() -> task<void, resumption_policy::immediate> {
            co_return ;
        };
        static auto res(coro());
        return task(res.get_promise());
    } else if constexpr (std::is_same_v<T, bool>) {

        static _details::BoolInit val;
        bool b = get_first(std::forward<Args>(args)...);
        return task((b?val.res_true:val.res_false).get_promise());

    } else {
        auto coro = [&]() -> task<T, resumption_policy::immediate> {
            co_return T(std::forward<Args>(args)...);
        };
        return task(coro().get_promise());
    }
}



template<typename T, typename Policy>
inline task<T, Policy> task<T, Policy>::set_exception() {
    auto e = std::current_exception();
    assert("task::set_exception() called outside of catch handler" && !!e);
    set_exception(e);
}

template<typename T, typename Policy>
inline task<T, Policy> task<T, Policy>::set_exception(std::exception_ptr e) {
    co_await std::suspend_never(); //force this to be coroutine
    assert("task::set_exception(e) called with no exception" && !!e);
    std::rethrow_exception(e);
}



}
#endif /* SRC_COCLASSES_TASK_H_ */
