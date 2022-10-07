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

///Synchronous await
/**
 * Synchronous await block whole thread until the awaiting promise is resolved
 * 
 * @param expr expression. 
 * @return result of await
 * 
 * @code
 *   //async await
 *   auto x = co_await expr;
 *   
 *   //sync await
 *   auto x = sync_await(expr);
 *   
 * @endcodes
 */
template<typename Expr> 
auto sync_await(Expr &&expr) -> decltype(expr.await_resume()) {
    if (expr.await_ready()) {
        return expr.await_resume();
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
    
    resume_lock::resume(expr.await_suspend(handle_t(&r)));

    std::unique_lock _(mx);
    cond.wait(_,[&]{return signal;});
    return expr.await_resume();
}

}




#endif /* SRC_COCLASSES_SYNC_AWAIT_H_ */
