#pragma once
#ifndef SRC_COCLASSES_CANCELABLE_TASK_H_
#define SRC_COCLASSES_CANCELABLE_TASK_H_
#include "exceptions.h"

#include <atomic>
#include <variant>


namespace cocls {

namespace _details {


    template<typename X, typename Y>
    auto has_await_transform_test(X &&x, Y &&y) -> decltype(std::declval<X>().await_transform(y));
    
    std::monostate has_await_transform_test(...);            

    template<typename X, typename Y>
    struct has_await_transform {
        static constexpr bool value = !std::is_same<std::monostate, decltype(has_await_transform_test(std::declval<X>(), std::declval<Y>()))>::value;
    };

    template<typename X>
    auto has_mark_canceled_test(X &&x) -> decltype(std::declval<X>().mark_canceled());
    
    std::monostate has_mark_canceled_test(...);            

    template<typename X>
    struct has_mark_canceled {
        static constexpr bool value = !std::is_same<std::monostate, decltype(has_mark_canceled_test(std::declval<X>()))>::value;
    };

    
}

///Cancelable coroutine
/**
 * @tparam Coro coroutine type (task<X>, lazy<X>)
 * 
 * Adds cancel() to coroutine. This allows to cancel coroutine before it executes co_await
 * or co_yield. Coroutine can be canceled only at these points. Once the coroutine is suspended,
 * you can't cancel it. You need to cancel it through the awaiter. Calling cancel in this
 * situation is stored and takes effect on next co_await
 * 
 * @note don't mix with lazy<T>::mark_canceled(). This allows to only cancel coroutine before it
 * started. However, if the coroutine future contains such a function, it is called as well
 * 
 * if coroutine wants to only check canceled state, it needs to call co_await std::suspend_never(); 
 */
template<typename Coro>
class cancelable: public Coro {
public:

    using super = Coro;

    class promise_type : public Coro::promise_type {
    public:
        
        using super = typename Coro::promise_type;
        ///uncaughtable exception
        
        struct uncaughtable_exception {};

        template<typename X>
        auto await_transform(X&& awt)  {
             if (_canceled.load(std::memory_order_acquire)) throw uncaughtable_exception();
             else {
                 if constexpr (_details::has_await_transform<super &, X>::value) {
                     return super::await_transform(std::forward<X>(awt));
                 } else {
                     return std::forward<X>(awt);
                 }
             }
        }
        
        void cancel() {
            _canceled.store(true,std::memory_order_release);
        }
        
        void unhandled_exception() {
            try {
                throw;
            } catch (uncaughtable_exception &) {
                try {
                    throw await_canceled_exception();
                }catch (...){
                    super::unhandled_exception();
                }
            } catch (...) {            
                super::unhandled_exception();
            }
        }
        
        cancelable get_return_object() {
              return cancelable(super::get_return_object());
          }
        
    protected:
        std::atomic<bool> _canceled = false;
        
    };
    
    using Coro::Coro;
    cancelable(const Coro &other):Coro(other) {}
    cancelable(Coro &&other):Coro(other) {}
    
    
    ///Cancel current coroutine before next co_await or co_yield is executed
    void cancel() {
        if constexpr(_details::has_mark_canceled<super &>::value) {
            Coro::mark_canceled();
        }
        static_cast<promise_type *>(this->get_promise())->cancel();        
    }
    
};


}



#endif /* SRC_COCLASSES_CANCELABLE_TASK_H_ */
