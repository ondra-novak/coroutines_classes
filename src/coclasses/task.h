/**
 * @file task.h  - task type coroutine
 */

#pragma once
#ifndef SRC_COCLASSES_TASK_H_
#define SRC_COCLASSES_TASK_H_

#include "awaiter.h"
#include "co_alloc.h"
#include "common.h" 
#include "exceptions.h"
#include "debug.h"
#include "resumption_policy.h"

#include <atomic>
#include <cassert>
#include <coroutine>
#include <future>
#include <optional>
#include <type_traits>
#include <concepts>

namespace cocls {

///Coroutine promise object - part of coroutine frame
/** This object is never used directly, but it is essential object to support coroutines
 * 
 */
template<typename T, typename Policy> class task_promise;

template<typename T> class task_promise_base;


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
 */
template<typename T = void, typename Policy = void> 
class task {
public:
    
    using promise_type = task_promise<T, Policy>;
    using promise_type_base = task_promise_base<T>;
    using value_type = T;

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
    
    template<std::convertible_to<T> X>
    explicit task(X &&x);
    
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
    co_awaiter<promise_type_base, true> operator co_await() {
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

    bool done() const {
        return is_ready();
    }
    
    ///Join the task synchronously, returns value
    decltype(auto) join() {
        co_awaiter<promise_type_base, true> aw(*_promise);
        return aw.wait();
    }
    
    
    ///Join the task synchronously, but do not pick result
    /**
     * Allows to synchronize code, waiting for task to finish, but
     * doesn't pick any result nor exception.
     */
    void sync() noexcept {
        co_awaiter<promise_type_base, true> aw(*_promise);
        aw.sync();
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
        static_cast<promise_type *>(_promise)->initialize_policy(std::forward<Args>(args)...);
    }
    
    
protected:
    promise_type_base *_promise;
    
    promise_type_base * get_promise() const {return _promise;}
};


template<typename T>
class task_promise_base: public coro_allocator  {
public:
    using AW = abstract_awaiter<true>;

    using Result = std::add_lvalue_reference_t<T>;
    
    std::atomic<AW *> _awaiter_chain;
    std::atomic<unsigned int> _ref_count;
    
    task_promise_base():_ref_count(0) {}          

    task_promise_base(const task_promise_base &) = delete;
    task_promise_base &operator=(const task_promise_base &) = delete;

    virtual ~task_promise_base() = default;
    
    
    virtual void destroy() =0;
    
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
            destroy();
        }
    }

    bool is_ready() const {
        return AW::is_ready(_awaiter_chain);
    }
    
    bool subscribe_awaiter(AW *aw) {
        return aw->subscibre_check_ready(_awaiter_chain);
    }

    virtual Result get_result() = 0;

    
};

template<typename T, typename Policy>
class task_promise_with_policy: public task_promise_base<T> {
public:

    template<typename Awt>
    decltype(auto) await_transform(Awt&& awt) noexcept {
        if constexpr (has_co_await<Awt>::value) {
            auto x = await_transform(awt.operator co_await());
            return x;
        } else if constexpr (has_global_co_await<Awt>::value) {
            auto x = await_transform(operator co_await(awt));
            return x;
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
    
    task_promise() {}
    
    union {
        T _v;
        std::exception_ptr _e;
    };
    
    using AW = typename task_promise_with_policy<T, Policy>::AW;
    template<typename X>
    auto return_value(X &&val)->std::enable_if_t<std::is_convertible_v<X,T> >{
        new(&_v) T(std::forward<X>(val));
        AW::mark_ready_data_resume(this->_awaiter_chain);
    }
    task<T, Policy> get_return_object() {
        return task<T, Policy>(static_cast<task_promise_base<T> *>(this));
    }
    virtual void destroy() override {
        auto h = std::coroutine_handle<task_promise>::from_promise(*this);
        h.destroy();
    }
    virtual T &get_result() override {
        if (AW::mark_processed_data(this->_awaiter_chain)) {
            return _v;
        }
        if (AW::mark_processed_exception(this->_awaiter_chain)) {
            std::rethrow_exception(_e);
        }
        throw value_not_ready_exception();
    }

    void unhandled_exception() {
        new(&_e) std::exception_ptr(std::current_exception());
        AW::mark_ready_exception_resume(this->_awaiter_chain);
    }
    
    ~task_promise() {       
        AW::cleanup_by_mark(this->_awaiter_chain,
                [this]{_v.~T();}, [this]{
                    if (!AW::is_processed(this->_awaiter_chain)) {
                        if (_e) debug_reporter::get_instance()
                                .report_exception(_e, typeid(task<T, Policy>));
                    }
                    _e.~exception_ptr();
                });
    }
};

template<typename Policy>
class task_promise<void, Policy>: public task_promise_with_policy<void, Policy> {
public:
    
    std::exception_ptr _e;
    
    using AW = typename task_promise_with_policy<void, Policy>::AW;
    auto return_void() {
        AW::mark_ready_data_resume(this->_awaiter_chain);
    }
    task<void, Policy> get_return_object() {
        return task<void, Policy>(this);
    }
    virtual void destroy() override {
        auto h = std::coroutine_handle<task_promise>::from_promise(*this);
        h.destroy();
    }
    void unhandled_exception() {
        _e =  std::current_exception();
        AW::mark_ready_exception_resume(this->_awaiter_chain);
    }
    void get_result() override {
        if (AW::mark_processed_data(this->_awaiter_chain)) {
            return ;
        }
        if (AW::mark_processed_exception(this->_awaiter_chain)) {
            std::rethrow_exception(_e);
        }
        throw value_not_ready_exception();
    }

    ~task_promise() {
        if (_e) debug_reporter::get_instance()
                .report_exception(_e, typeid(task<void, Policy>));
    }
};

template<typename T>
class task_manual_resolve: public task_promise_base<T> {
public:
    using AW = typename task_promise_base<T>::AW;
    
    
    template<typename ... Args>
    task_manual_resolve(Args &&...args ):_value(std::forward<Args>(args)...) {
        AW::mark_ready_data_resume(this->_awaiter_chain);
    }
            
    virtual void destroy() override {
        delete this;
    }

    T &get_result() override {
        return _value;
    }
    
    T _value;
    
};

template<>
class task_manual_resolve<void>: public task_promise_base<void> {
public:
    using AW = typename task_promise_base<void>::AW;
    
    
    template<typename ... Args>
    task_manual_resolve() {
        AW::mark_ready_data_resume(this->_awaiter_chain);
    }
            
    virtual void destroy() override {
        delete this;
    }

    void get_result() override {}
    
    
};

template<typename T>
class task_manual_static_resolve: public task_manual_resolve<T> {
public:
    using task_manual_resolve<T>::task_manual_resolve;
    
    void *operator new(std::size_t sz) {
        return ::operator new(sz);
    }
    void operator delete(void *ptr, std::size_t sz) {
        ::operator delete(ptr);
    }
};


template<typename T, typename P>
template<std::convertible_to<T> X>
inline task<T,P>::task(X &&x) {

    if constexpr(std::is_base_of_v<task_promise_base<T>, std::remove_pointer_t<X> >) {
        _promise = x;
        if (_promise) _promise->add_ref();
    }
    else if constexpr(std::is_same<X,bool>::value) {
        if (x) {
            static task<T,P> inst(new task_manual_static_resolve<bool>(true));
            *this = inst;
        } else {
            static task<T,P> inst(new task_manual_static_resolve<bool>(false));
            *this = inst;
        }
    } 
    else {
        *this = task<T,P>(new task_manual_resolve<T>(std::forward<X>(x)));
    }
}

template<typename T, typename P>
task<T,P>::task():_promise(nullptr) {
    if constexpr(std::is_void<T>::value) {
        static task<T,P> inst(new task_manual_static_resolve<void>());
        *this = inst;
    }
}

namespace _details {

template<typename T, typename Fn>
struct task_from_return_value {
    using type = task<decltype(std::declval<Fn>()(std::declval<T>()))>;
};

template<typename Fn>
struct task_from_return_value<void, Fn> {
    using type = task<decltype(std::declval<Fn>()())>;
};

template<typename T, typename Fn>
using task_from_return_value_t = typename task_from_return_value<T,Fn>::type;

}


template<typename T> 
    struct is_task :std::integral_constant<bool, false> {};
template<typename T, typename P> 
    struct is_task<task<T,P> > : std::integral_constant<bool, true> {};
template<typename T> 
    struct task_result;
template<typename T, typename P> 
    struct task_result<task<T,P> > {using type = T;};






}


#endif /* SRC_COCLASSES_TASK_H_ */
