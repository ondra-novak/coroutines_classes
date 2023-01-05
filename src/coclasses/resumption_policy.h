/** @file resumption_policy.h
 * 
 *  Resumption policy is policy about how each task is resumed. 
 *  
 *  Resumption policy can be specified on task, or on an awaiter. Generators has
 *  no resumption policy, they are always resumed on thread which calls them.
 *  
 *  Resumption policy is enforced to an awaiter, if the awaiter supports function
 *  set_resumption_policy. Not all awaiters can support this function. For example
 *  awaiting on thread pool expects, that coroutine will be transfered to the thread
 *  pool with no exception. However this needs for awaiters that only sends signal, where
 *  resumption is side effect of such signal.
 *  
 *  @see resumption_policy_concept
 *  
 *  
 * 
 */
#ifndef SRC_COCLASSES_RESUMPTION_POLICY_H_
#define SRC_COCLASSES_RESUMPTION_POLICY_H_
#include "common.h"

#include <coroutine>
#include <variant>

namespace cocls {

namespace _details {
    
    template<typename X>
    auto test_has_co_await(X &&x) -> decltype(x.operator co_await());
    std::monostate test_has_co_await(...);

    template<typename X, typename Y>
    auto test_has_set_resumption_policy(X &&x, Y &&y) -> decltype(x.set_resumption_policy(std::forward<X>(x),std::forward<Y>(y)));
    std::monostate test_has_set_resumption_policy(...);

    template<typename X>
    auto test_has_wait(X &&x) -> decltype(x.wait());
    std::monostate test_has_wait(...);

    template<typename X, typename Y>
    auto test_has_subscribe_awaiter(X &&x, Y &&y) -> decltype(x.subscribe_awaiter(y));
    std::monostate test_has_subscribe_awaiter(...);

    template<typename X>
    auto test_has_join(X &&x) -> decltype(x.join());
    std::monostate test_has_join(...);
    
    template<typename X>
    auto test_can_co_await(X &&x) -> decltype(operator co_await(std::forward<X>(x)));
    std::monostate test_can_co_await(...);

}

///Determines whether specified awaiter object has operator co_await()
/**
 * @tparam X awaiter to test
 * @return value contains true, if the awaiter has such operator, or false if not
 */
template<typename X> 
using has_co_await = std::negation<std::is_same<std::monostate, decltype(_details::test_has_co_await(std::declval<X>()))> >;
template<typename X> 
using has_global_co_await = std::negation<std::is_same<std::monostate, decltype(_details::test_can_co_await(std::declval<X>()))> >;

///Determines whether specified awaiter object has set_resumption_policy() function
/**
 * @tparam X awaiter to test
 * @return value contains true, if there is such function, or false if not
 * @tparam Y policy object
 */
template<typename X, typename Y> 
using has_set_resumption_policy = std::negation<std::is_same<std::monostate, decltype(_details::test_has_set_resumption_policy(std::declval<X>(), std::declval<Y>()))> >;

template<typename X> 
using has_wait = std::negation<std::is_same<std::monostate, decltype(_details::test_has_wait(std::declval<X>()))> >;
template<typename X, typename Y> 
using has_subscribe_awaiter = std::negation<std::is_same<std::monostate, decltype(_details::test_has_subscribe_awaiter(std::declval<X>(),std::declval<Y>()))> >;
template<typename X> 
using has_join = std::negation<std::is_same<std::monostate, decltype(_details::test_has_join(std::declval<X>()))> >;


///definition of various resumption policies
namespace resumption_policy {

///Resumption policy concept - template to create resumption policy
struct _policy_concept {
    
    ///Specifies how the coroutine is initially started. It is awaiter type.
    /**Its constructor must accept the resumption policy object */ 
    struct initial_awaiter;
    ///mandatory - handles resumption
    void resume(std::coroutine_handle<> h);
    ///optional - allows to initialize the polici on a task
    /**
     * Tasks can be created with resumption policy, however, there is now way how
     * to pass parameters to the policy. In this case, these tasks should be always
     * created as suspended. There is a function task<>::initialize_policy() which
     * can be used to pass arguments to the policy. After then, the task can be resumed
     * 
     * Resumption policies whithout arguments don't need such function     
     */
    void initialize_policy(...);
};


///Awaiter concept to work with resumption policy
struct _awaiter_concept {
    
    ///await ready standard implementation
    bool await_ready();
    ///await suspend standard implementation
    bool await_suspend(std::coroutine_handle<> h);
    ///await resume standard implementation
    auto await_resume();
    
    ///creates a copy of awaiter with specified policy
    /**
     * Function is called from task<>::await_transform. It passes policy of the task
     * to enforce his policy. Result of the function is awaiter with given policy. Then
     * the awaiter must respect that resumption policy
     * 
     * @param _this instance of this type or derived type. This allows to make wrapping awaiter
     * with top-most derived class
     * 
     * @param p resumption policy
     * 
     * @return new awaiter which respects given resumption policy
     */
    template<typename _This, typename Policy>
    static auto set_resumption_policy(_This _this,Policy p);
    
};

struct initial_suspend_never: public std::suspend_never {
    template<typename Policy> initial_suspend_never(Policy &p) {}
};

template<typename Policy>
struct initial_resume_by_policy: public std::suspend_always {
    Policy &_p;
    initial_resume_by_policy(Policy &p):_p(p) {}
    initial_resume_by_policy(const initial_resume_by_policy &p) = default;
    initial_resume_by_policy &operator=(const initial_resume_by_policy &p) = delete;
    constexpr void await_suspend(std::coroutine_handle<> h) const noexcept {
        _p.resume(h);        
    }
};




    ///Resumption of a coroutine is made recursive by calling resume on current stack frame
    /**
     * When coroutine is resumed, resumption is executed on current stack. 
     * Execution is immediate
     */
    struct immediate {
        using initial_awaiter = initial_suspend_never;
        static void resume(std::coroutine_handle<> h) noexcept {h.resume();}
    };    
    struct queued;
    struct parallel;
    struct thread_pool;

    ///when resumption policy is not specified
    /** This template can be overwritten by specializing to unspecified<void> 
     * 
     * Default resumption policy is start_immediately_then_queued. This 
     * causes that coroutines acts as normal functions, only when a coroutine 
     * suspended, then its resumption is handled through thread's queue
     * 
     * */
    template<typename> using unspecified = queued;; 

}

}


#endif /* SRC_COCLASSES_RESUMPTION_POLICY_H_ */
