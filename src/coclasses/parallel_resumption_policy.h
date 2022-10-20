#pragma once

#ifndef SRC_COCLASSES_PARALLEL_RESUMPTION_POLICY_H_
#define SRC_COCLASSES_PARALLEL_RESUMPTION_POLICY_H_

#include <coroutine>
#include "queued_resumption_policy.h"

namespace cocls {


namespace resumption_policy {
///Parallel resumption policy - creates a thread and resumes the coroutine in it
    struct parallel {
        void resume(std::coroutine_handle<> h) {
            std::thread thr([h]{
                resumption_policy::queued::resume(h);
            });
            thr.detach();
        }
        
    };
}


}



#endif /* SRC_COCLASSES_PARALLEL_RESUMPTION_POLICY_H_ */
