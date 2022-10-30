/**
 * @file task.h  - task type coroutine
 */

#pragma once
#ifndef SRC_COCLASSES_TASK_H_
#define SRC_COCLASSES_TASK_H_

#include "awaiter.h"
#include "common.h" 
#include "exceptions.h"
#include "debug.h"
#include "future_var.h"
#include "poolalloc.h"
#include "resumption_policy.h"

#include <atomic>
#include <cassert>
#include <coroutine>
#include <future>
#include <optional>
#include <type_traits>



namespace cocls {





///Coroutine promise object - part of coroutine frame
/** This object is never used directly, but it is essential object to support coroutines
 * 
 */
template<typename T, typename Policy> class task_promise;

template<typename T> class task_promise_base;


template<std::size_t space> class static_task_storage;

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
 */
template<typename T = void, typename Policy = void> 
class task {
public:
    
    using promise_type = task_promise<T, Policy>;
    using promise_type_base = task_promise_base<T>;

    ///You can create empty task, which can be initialized later by assign
    /** For the task<void>, the object is already initialized and co_await on such task
     * is always resolved
     */ 
    task();
    
    ///Initializes task future with direct value
    /**
     * @param x value to initialize the task future
     * 
     * This allows to generate task<> result without executing coroutine
     * 
     * @note There will be always a coroutine which is executed to handle this
     * feature, there is no much optimization. 
     */
    template<typename X, typename = decltype(T(std::declval<X>()))>
    explicit task(X &&x);
    
    ///task is internaly constructed from pointer to a promise  
    task(std::coroutine_handle<promise_type_base> h): _h(h) {
        get_promise()->add_ref();
    }
    ///destruction of task decreases reference
    ~task() {
        if (_h) get_promise()->release_ref();
    }
    
    ///you can copy task, which just increases references
    task(const task &other):_h(other._h) {
        get_promise()->add_ref();
    }
    ///you can move task
    task(task &&other):_h(other._h) {
        other._h= nullptr;
    }
    ///you can assign
    task &operator=(const task &other){
        if (this != &other) {
            if (_h) get_promise()->release_ref();
            _h= other._h;
            get_promise()->add_ref();
        }
        return *this;
    }
    ///you can move-assign
    task &operator=(task &&other) {
        if (this != &other) {
            if (_h) get_promise()->release_ref();
            _h = other._h;
            other._h = nullptr;        
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
    co_awaiter<promise_type_base, true> operator co_await() {
        return *get_promise();
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
        return get_promise()->is_ready();
    }

    ///Join the task synchronously, returns value
    decltype(auto) join() {
        co_awaiter<promise_type_base, true> aw(*get_promise());
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
     *@endcode
     *
     */
    template<typename resumption_policy>
    decltype(auto) join(resumption_policy &&policy) {
        return co_awaiter<promise_type, true>::set_resumption_policy(operator co_await(), std::forward<resumption_policy>(policy)); 
    }

    
    ///Retrieve unique coroutine identifier
    coro_id get_id() const {
        return _h.address();
    }
    
   
    ///Determines whether object contains a valid task
    bool valid() const {
        return _h!=nullptr;
    }
    
    
    ///Allows to convert this object to object with unspecified policy
    /**
     * This helps to work with task<> objects, where policy has no meaning
     * 
     * Calling initialize_policy() after conversion is UB
     */ 
    operator task<T>() {
        return task<T>(_h);
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
        static_cast<promise_type *>(get_promise())->initialize_policy(std::forward<Args>(args)...);
    }
    
protected:
    std::coroutine_handle<promise_type_base> _h;
    
    
    promise_type_base *get_promise() const {
        return &_h.promise();
    }
};


class task_storage {
public:
    task_storage() = default;
    task_storage(task_storage &) = delete;
    task_storage &operator=(task_storage &) = delete;
    virtual ~task_storage()=default;
    
    virtual void *alloc(std::size_t sz) = 0;
    virtual std::size_t capacity() const = 0; 
};



template<typename T>
class task_promise_base: public coro_promise_base  {
public:
    using AW = abstract_awaiter<true>;

    std::atomic<AW *> _awaiter_chain;
    std::atomic<unsigned int> _ref_count;
    future_var<T> _value;

    task_promise_base():_ref_count(0) {}

    task_promise_base(const task_promise_base &) = delete;
    task_promise_base &operator=(const task_promise_base &) = delete;
    ~task_promise_base() {
        if (!AW::is_processed(_awaiter_chain)) {
            std::exception_ptr e = _value.exception_ptr();
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

    

    
    bool is_ready() const {
        return AW::is_ready(_awaiter_chain);
    }
    
    bool subscribe_awaiter(AW *aw) {
        return aw->subscibre_check_ready(_awaiter_chain);
    }
    
    decltype(auto) get_result() {
        if (AW::mark_processed(_awaiter_chain)) {
            return _value.get();
        } else {
            throw value_not_ready_exception();
        }
        
    }
    
    void unhandled_exception() {
        _value.unhandled_exception();
        AW::mark_ready_resume(_awaiter_chain);
    }
    
    void *operator new(std::size_t sz) {
        void *r = coro_promise_base::operator new(sz+1);
        bool *x = reinterpret_cast<bool *>(r)+sz;
        *x = false;
        return r;
    }

    template< typename ... Args>
    void *operator new(std::size_t sz, task_storage &storage, Args && ...) {
        void *r = storage.alloc(sz+1);
        bool *x = reinterpret_cast<bool *>(r)+sz;
        *x = true;
        return r;
    }

    template<typename ... Args>
    void operator delete(void *ptr, task_storage &storage, Args && ... ) {
        //empty
    }

    void operator delete(void *ptr, std::size_t sz) {
        bool *x = reinterpret_cast<bool *>(ptr)+sz;
        if (!*x) coro_promise_base::operator delete(ptr, sz+1);
    }
};


template<typename T, typename Policy>
class task_promise_with_policy: public task_promise_base<T> {
public:

    template<typename Awt>
    decltype(auto) await_transform(Awt&& awt) noexcept {
        if constexpr (has_co_await<Awt>::value) {
            return await_transform(awt.operator co_await());
        } else if constexpr (has_global_co_await<Awt>::value) {
            return operator co_await(awt);
        } else if constexpr (has_set_resumption_policy<Awt, Policy>::value) {
            return awt.set_resumption_policy(std::forward<Awt>(awt), _policy);
        } else {
            return std::forward<Awt>(awt);
        }
    }

    using initial_awaiter = typename std::remove_reference<Policy>::type::initial_awaiter;

    initial_awaiter initial_suspend()  noexcept {
        ++this->_ref_count;
        return initial_awaiter(_policy);
    }

    template<typename ... Args>
    void initialize_policy(Args &&... args) {
        _policy.initialize_policy(std::forward<Args>(args)...);
    }

    [[no_unique_address]]  Policy _policy;
};

template<typename T>
class task_promise_with_policy<T, void>: public task_promise_with_policy<T, resumption_policy::unspecified<void> > {
};


template<typename T, typename Policy>
class task_promise: public task_promise_with_policy<T, Policy> {
public:
    using AW = typename task_promise_with_policy<T, Policy>::AW;
    template<typename X>
    auto return_value(X &&val)->decltype(std::declval<future_var<T> >().emplace(std::forward<X>(val))) {
        this->_value.emplace(std::forward<X>(val));
        AW::mark_ready_resume(this->_awaiter_chain);
    }
    task<T, Policy> get_return_object() {
        return task<T, Policy>(std::coroutine_handle<task_promise_base<T> >::from_promise(*this));
    }
};


template<typename Policy>
class task_promise<void, Policy>: public task_promise_with_policy<void, Policy> {
public:
    using AW = typename task_promise_with_policy<void, Policy>::AW;
    auto return_void() {
        this->_value.emplace();
        AW::mark_ready_resume(this->_awaiter_chain);
    }
    task<void, Policy> get_return_object() {
        return task<void, Policy>(std::coroutine_handle<task_promise_base<void> >::from_promise(*this) );
    }
    

};
namespace _details {
    inline task<void, resumption_policy::immediate> coro_void(task_storage &) {
        co_return;
    }
    template<typename X>
    inline task<X, resumption_policy::immediate> coro_resolve(task_storage &, X x) {
        co_return std::forward<X>(x);
    }
    template<typename X>
    inline task<X, resumption_policy::immediate> coro_resolve(X x) {
        co_return std::forward<X>(x);
    }
}

///Represents preallocated space for the task
/**
 * @tparam space preallocated spaces
 * 
 * Pass reference of this object as the first argument of a coroutine
 * 
 * @code 
 * task<int> example(static_task_storage<1000> &, int args)
 *      co_return args;
 * }
 * 
 * 
 * {
 *    static_task_storage<1000> storage;
 *    int res = example(storage, 42).join();
 *    //...
 * }
 * @endcode
 */
template<std::size_t space>
class static_task_storage:public task_storage {
public:
    virtual void *alloc(std::size_t sz) override { 
        assert(sz <= space); //space is too small to fit the cooroutine frame;
        return _buffer;
    }
    virtual std::size_t capacity() const override {return space;}
protected:
    char _buffer[space];
};



template<typename T, typename P>
template<typename X, typename>
inline task<T,P>::task(X &&x) {
    if constexpr(std::is_same<X,bool>::value) {
        if (x) {
            static static_task_storage<sizeof(void *)*11> storage; 
            static task<bool> v_true = _details::coro_resolve(storage, true);
            *this = v_true;
        } else {
            static static_task_storage<sizeof(void *)*11> storage; 
            static task<bool> v_false = _details::coro_resolve(storage, false);
            *this = v_false;
        }
    } else {
        *this = _details::coro_resolve(std::forward<X>(x));
    }
}

template<typename T, typename P>
task<T,P>::task():_h(nullptr) {
    if constexpr(std::is_void<T>::value) {
        static static_task_storage<sizeof(void *)*11> storage; 
        static task<void> prealloc = _details::coro_void(storage); 
        *this = prealloc;
    }
}






}


#endif /* SRC_COCLASSES_TASK_H_ */
