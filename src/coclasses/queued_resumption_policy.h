#pragma once
#ifndef SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_
#define SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_

#include <coroutine>
#include <queue>

#include "resumption_policy.h"


namespace cocls {

namespace _details {

class queued_resumption_control {
public:

    static thread_local queued_resumption_control instance;

    void resume(std::coroutine_handle<> h) {
        if (!_active) {
            _active = true;
            h.resume();
            while (!_queue.empty()) {
                h = _queue.front();
                _queue.pop();
                h.resume();
            }
            _active = false;
        } else {
            _queue.push(h);
        }
    }
    


    bool is_active() const {return _active;}

protected:
    bool _active = false;
    std::queue<std::coroutine_handle<> > _queue;

};


inline thread_local queued_resumption_control queued_resumption_control::instance;

}


namespace resumption_policy {

///Implements queue on the current thread. If the coroutine is resumed, it is put into queue and resumed after current coroutine is suspended
/**
 * the rules are - on the first resume, the coroutine is resumed immediately. 
 * Nested resumes are put into queue and resumed after current coroutine is suspended or
 * finished
 * 
 * A task started under this policy is executed immediately.
 * 
 * This resumption policy is recommended and it is default when resumption policy
 * is unspecified
 * 
 */
struct queued {
    struct initial_awaiter {
        initial_awaiter(queued &) {}
        bool await_ready() noexcept {
            //check whether queue is already active
            //awaiter is ready if queue exists, otherwise we need to install queue then return false
            return _details::queued_resumption_control::instance.is_active();
        }
        void await_suspend(std::coroutine_handle<> h) {
            //this is called only when we need to install queue
            //this resume does it automatically for us
            _details::queued_resumption_control::instance.resume(h);
        }
        static constexpr void await_resume() noexcept {}
    };
    
    ///resume in queue
    static void resume(std::coroutine_handle<> h) {
        _details::queued_resumption_control::instance.resume(h);
    }
    static constexpr auto resume_handle(std::coroutine_handle<> h) {
        return h;
    }
};

}

}



#endif /* SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_ */
