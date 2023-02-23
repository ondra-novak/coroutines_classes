/** @file parallel_resumption_policy.h */
#pragma once

#ifndef SRC_COCLASSES_PARALLEL_RESUMPTION_POLICY_H_
#define SRC_COCLASSES_PARALLEL_RESUMPTION_POLICY_H_

#include <coroutine>
#include <thread>
#include "queued_resumption_policy.h"

namespace cocls {


namespace resumption_policy {
///Parallel resumption policy - creates a thread and resumes the coroutine in it
    struct parallel {
        struct initial_awaiter : std::suspend_always {
            initial_awaiter(parallel &) {}
            static void await_suspend(std::coroutine_handle<> h)  {
                resume(h);
            }
        };
        static void resume(std::coroutine_handle<> h) {
            std::thread thr([h]{
                resumption_policy::queued::resume(h);
            });
            thr.detach();
        }
        ///symmetric transfer on parallel is performed directly with no thread creation
        /**
         * This assumes, that coroutine at current thread is finishing, so thread
         * is no longer occupuid and can be reused by awaiting coroutine. This
         * saves resource
         */
        std::coroutine_handle<> resume_handle(std::coroutine_handle<> h) {
            return h;
        }

    };
}


}



#endif /* SRC_COCLASSES_PARALLEL_RESUMPTION_POLICY_H_ */
