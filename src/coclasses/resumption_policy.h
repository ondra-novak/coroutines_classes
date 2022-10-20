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
#include <coroutine>

namespace cocls {

///Resumption policy concept - template to create resumption policy
struct resumption_policy_concept {
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
struct awaiter_concept {
    
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
     * @param p resumption policy
     * @return new awaiter which respects given resumption policy
     */
    template<typename Policy>
    auto set_resumption_policy(Policy p);
    
};



struct recursive_resumption_policy {    
    static void resume(std::coroutine_handle<> h) noexcept {h.resume();}
};




///definition of various resumption policies
namespace resumption_policy {
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
