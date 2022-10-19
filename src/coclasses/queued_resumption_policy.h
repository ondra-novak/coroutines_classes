#pragma once
#ifndef SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_
#define SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_

#include <coroutine>
#include <queue>


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

struct queued_resumption_policy {
    void resume(std::coroutine_handle<> h) {
        _details::queued_resumption_control::instance.resume(h);
    }
};

}



#endif /* SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_ */
