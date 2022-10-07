#pragma once
#ifndef SRC_COCLASSES_SYNC_AWAIT_H_
#define SRC_COCLASSES_SYNC_AWAIT_H_

#include "common.h"
#include "resume_lock.h"
#include "handle.h"

#include <condition_variable>
#include <mutex>
namespace cocls {

template<typename Expr> 
auto sync_await(Expr &&x) -> decltype(x.operator co_await().await_resume()) {
    return sync_await(x.operator co_await());
}

template<typename Expr> 
auto sync_await(Expr &&x) -> decltype(x.await_resume()) {
    if (x.await_ready()) {
        return x.await_resume();
    }
    std::mutex mx;
    std::condition_variable cond;
    bool signal = false;
    auto a = [&]{
        std::unique_lock _(mx);
        signal = true;
        cond.notify_one();
    };
    cb_resumable_t<decltype(a)> r(std::move(a));    
    
    resume_lock::resume(x.await_suspend(handle_t(&r)));

    std::unique_lock _(mx);
    cond.wait(_,[&]{return signal;});
    return x.await_resume();
}

}




#endif /* SRC_COCLASSES_SYNC_AWAIT_H_ */
