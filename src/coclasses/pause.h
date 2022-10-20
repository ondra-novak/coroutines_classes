#pragma once
#ifndef SRC_COCLASSES_PAUSE_H_
#define SRC_COCLASSES_PAUSE_H_
#include "queued_resumption_policy.h"

namespace cocls {

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
template<typename policy = resumption_policy::queued >
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



}



#endif /* SRC_COCLASSES_PAUSE_H_ */
