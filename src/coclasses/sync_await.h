/**
 * @file sync_await.h - introduces sync_await operator
 */
#pragma once
#ifndef SRC_COCLASSES_SYNC_AWAIT_H_
#define SRC_COCLASSES_SYNC_AWAIT_H_

#include "common.h"
#include "no_alloc.h"




#include <condition_variable>
#include <mutex>
namespace cocls {



struct sync_await_tag{

    template<typename Expr>
    static task<std::remove_reference_t<decltype(std::declval<Expr>().await_resume())>, resumption_policy::immediate>  sync_await_coro(Expr &expr) {
        co_return co_await expr;    
    }

    
    template<typename Expr> 
    auto operator,(Expr &&x) -> std::remove_reference_t<decltype(x.operator co_await().await_resume())> {
        return operator,(x.operator co_await());
    }

    template<typename Expr> 
    auto operator,(Expr &&expr) -> std::remove_reference_t<decltype(expr.await_resume())> {

        
        return sync_await_coro(expr).join();
    }

    
};

}

///Sync await - similar to co_await, but can be used in outside of coroutine
/**
 * sync_await &lt;expr&gt; - waits for result synchronously
 */
#define sync_await ::cocls::sync_await_tag(), 




#endif /* SRC_COCLASSES_SYNC_AWAIT_H_ */
