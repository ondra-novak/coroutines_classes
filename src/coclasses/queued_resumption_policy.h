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
        _queue.push(h);
        flush_queue();
    }

    void flush_queue() {
        if (!_active) {
            _active = true;
            while (!_queue.empty()) {
                auto h = _queue.front();
                _queue.pop();
                h.resume();
            }
            _active = false;
        }
    }


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
 * A task started under this policy is executed immediately when started from normal
 * function, but it is queued and postponed when started from a coroutine.
 * 
 * This resumption policy is recommended and it is default when resumption policy
 * is unspecified
 * 
 */
struct queued {
    ///resume in queue
    static void resume(std::coroutine_handle<> h) {
        _details::queued_resumption_control::instance.resume(h);
    }
    ///flush queue, executing all queued coroutines now
    static void flush_queue() {
        _details::queued_resumption_control::instance.flush_queue();
    }
};

}
}



#endif /* SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_ */
