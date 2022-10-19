#pragma once

#ifndef SRC_COCLASSES_PARALLEL_RESUMPTION_POLICY_H_
#define SRC_COCLASSES_PARALLEL_RESUMPTION_POLICY_H_

#include <coroutine>

namespace cocls {


///Parallel resumption policy - creates a thread and resumes the coroutine in it
struct parallel_resumption_policy {
    void resume(std::coroutine_handle<> h) {
        std::thread thr([h]{
            h.resume();
        });
        thr.detach();
    }
};


}



#endif /* SRC_COCLASSES_PARALLEL_RESUMPTION_POLICY_H_ */
