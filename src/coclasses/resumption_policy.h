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

}

///Determines whether specified awaiter object has operator co_await()
/**
 * @tparam X awaiter to test
 * @return value contains true, if the awaiter has such operator, or false if not
 */
template<typename X> 
using has_co_await = std::negation<std::is_same<std::monostate, decltype(_details::test_has_co_await(std::declval<X>()))> >;

///Determines whether specified awaiter object has set_resumption_policy() function
/**
 * @tparam X awaiter to test
 * @tparam Y policy object
 * @return value contains true, if there is such function, or false if not
 */
template<typename X, typename Y> 
using has_set_resumption_policy = std::negation<std::is_same<std::monostate, decltype(_details::test_has_set_resumption_policy(std::declval<X>(), std::declval<Y>()))> >;



///definition of various resumption policies
namespace resumption_policy {

///Resumption policy concept - template to create resumption policy
struct _policy_concept {
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



struct recursive_resumption_policy {    
    static void resume(std::coroutine_handle<> h) noexcept {h.resume();}
};




    ///Resumption of a coroutine is made recursive by calling resume on current stack frame
    /**
     * When coroutine is resumed, resumption is executed on current stack. 
     * Execution is immediate
     */
    struct immediate {
        static void resume(std::coroutine_handle<> h) noexcept {h.resume();}
    };    
    struct queued;
    struct parallel;
    struct thread_pool;        

    ///when resumption policy is not specified
    /** This template can be overwritten by specializing to unspecified<void> */
    template<typename> using unspecified = queued; 

}

}



#endif /* SRC_COCLASSES_RESUMPTION_POLICY_H_ */
