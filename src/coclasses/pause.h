/** @file pause.h */
#pragma once
#ifndef SRC_COCLASSES_PAUSE_H_
#define SRC_COCLASSES_PAUSE_H_
#include "queued_resumption_policy.h"

namespace cocls {

template<typename policy = void>
class pause;

///Awaitable object, which pauses and resumes the coroutine under different resumption policy
/**
 * @tparam policy resumption policy class.
 *
 * Default policy is queued_resumption_policy, which causes, that coroutine is paused and
 * other queued coroutines are resumed. After all these coroutines finish their task, this
 * coroutine continues
 *
 * Use parallel_resumption_policy to transfer this coroutine to a separatedly allocated thread
 * (starts new thread)
 *
 */

template<typename policy>
class pause: private policy {
public:
    template<typename ... Args>

    explicit pause(Args && ... args):policy(std::forward<Args>(args)...) {}
    static bool await_ready() noexcept {return false;}
    void await_suspend(std::coroutine_handle<> h) noexcept {
        policy::resume(h);
    }
    static void await_resume() noexcept {}

};

template<>
class pause<void> {
public:
    
    template<typename policy>
    static decltype(auto) set_resumption_policy(pause<void>, policy &&p) {
        return pause<typename std::remove_reference<policy>::type>(std::forward<policy>(p));
    }
    
};



}



#endif /* SRC_COCLASSES_PAUSE_H_ */
