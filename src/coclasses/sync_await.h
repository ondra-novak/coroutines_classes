#pragma once
#ifndef SRC_COCLASSES_SYNC_AWAIT_H_
#define SRC_COCLASSES_SYNC_AWAIT_H_

#include "common.h"
#include "resume_lock.h"

#include "reusable.h"

#include <condition_variable>
#include <mutex>
namespace cocls {



struct sync_await_tag{
    using sync_await_storage = reusable_memory<std::array<void *, 16> >;

    template<typename Expr>
    static reusable<task<std::remove_reference_t<decltype(std::declval<Expr>().await_resume())> >, sync_await_storage> sync_await_coro(sync_await_storage &, Expr &expr) {
        co_return co_await expr;    
    }

    
    template<typename Expr> 
    auto operator,(Expr &&x) -> std::remove_reference_t<decltype(x.operator co_await().await_resume())> {
        return operator,(x.operator co_await());
    }

    template<typename Expr> 
    auto operator,(Expr &&expr) -> std::remove_reference_t<decltype(expr.await_resume())> {

        sync_await_storage stor;
        
        return sync_await_coro(stor, expr).join();
    }

    
};

}

///Sync await - similar to co_await, but can be used in outside of coroutine
/**
 * sync_await <expr> - waits for result synchronously
 */
#define sync_await ::cocls::sync_await_tag(), 




#endif /* SRC_COCLASSES_SYNC_AWAIT_H_ */
