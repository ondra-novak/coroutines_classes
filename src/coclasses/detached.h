/*
 * detached.h
 *
 *  Created on: 9. 1. 2023
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_DETACHED_H_
#define SRC_COCLASSES_DETACHED_H_



#include "debug.h"

#include <coroutine>
#include "resumption_policy.h"


namespace cocls {


///Detached coroutine, very simple coroutine which runs detached and has no return value
/**
 * This is minimalistic coroutine.
 * - has no result (return void)
 * - cannot be awaited
 * - cannot be joined
 * - doesn't contains storage
 * - exceptions are ignored
 *
 * It is supposed to be used along with other synchronization primitive, such a future.
 * You pass promise as argument. Then counterpart future object can be awaited.
 *
 * For such basic usage, the coroutine doesn't contains storage for the value,
 * and also cannot be awaited. For this reason, you don't need to save coroutine
 * result object. The coroutine is detached. Once coroutine is finished, it automatically
 * destroys its frame
 *
 * @tparam _Policy resumption policy. Default value is void - which uses
 * default resumption policy
 */
template<typename _Policy = void>
class detached {
public:


    class promise_type: public coro_promise_base, public coro_policy_holder<_Policy>{
    public:

          using Policy = typename coro_policy_holder<_Policy>::Policy;

          detached<_Policy> get_return_object() {
              return detached<_Policy>(this);
          }

          std::suspend_never final_suspend() noexcept {return {}; }


          void return_void() {}

          void unhandled_exception() {
              debug_reporter::current_instance
                      ->report_exception(std::current_exception(), typeid(detached<Policy>));
          }


    };

    ///intialize resumption policy
    /**
     * For some resumption policies, the policy must be extra initialized, otherwise
     * the coroutine doesn't start. In such case, you need to call initialize_policy
     * to actually to allow coroutine to be resumed according to given policy.
     *
     * @tparam Args
     * @param args
     */
    template<typename ... Args>
    void initialize_policy(Args &&... args) {
        _p->initialize_policy(std::forward<Args>(args)...);
    }


protected:
    detached(promise_type *p):_p(p) {}

    promise_type *_p;
};


}



#endif /* SRC_COCLASSES_DETACHED_H_ */
