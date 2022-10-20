/*
 * execution_policy.h
 *
 *  Created on: 19. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_RESUMPTION_POLICY_H_
#define SRC_COCLASSES_RESUMPTION_POLICY_H_
#include <coroutine>

namespace cocls {


///Resumption of a coroutine is made recursive by calling resume on current stack frame
struct recursive_resumption_policy {    
    static void resume(std::coroutine_handle<> h) noexcept {h.resume();}
};

///Resumption of a coroutine is enqueued on a current thread and it is resumed once the current coroutine is suspended
struct queued_resumption_policy;


///Resumption of a coroutine is executed in separate thread
struct parallel_resumption_policy;

///Resumption of a coroutine is executed in a thread pool - the instance to the thread pool is passed as an argument
struct thread_pool_resumption_policy;


template<typename> using default_resumption_policy = queued_resumption_policy; 

}



#endif /* SRC_COCLASSES_RESUMPTION_POLICY_H_ */
