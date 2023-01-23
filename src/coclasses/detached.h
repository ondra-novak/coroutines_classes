/*
 * detached.h
 *
 *  Created on: 9. 1. 2023
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_DETACHED_H_
#define SRC_COCLASSES_DETACHED_H_

#include "co_alloc.h"

#include "debug.h"

#include <coroutine>


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

    using Policy = std::conditional_t<std::is_void_v<_Policy>,resumption_policy::unspecified<void>,_Policy>;

    class promise_type: public coro_allocator {
    public:

        template<typename Awt>
          decltype(auto) await_transform(Awt&& awt) noexcept {
              if constexpr (has_co_await<Awt>::value) {
                  auto x = await_transform(awt.operator co_await());
                  return x;
              } else if constexpr (has_global_co_await<Awt>::value) {
                  auto x = await_transform(operator co_await(awt));
                  return x;
              } else if constexpr (has_set_resumption_policy<Awt, Policy>::value) {
                  return awt.set_resumption_policy(std::forward<Awt>(awt), _policy);
              } else {
                  return std::forward<Awt>(awt);
              }
          }

          using initial_awaiter = typename std::remove_reference<Policy>::type::initial_awaiter;

          initial_awaiter initial_suspend()  noexcept {
              return initial_awaiter(_policy);
          }
          
          std::suspend_never final_suspend() noexcept {return {}; }

          template<typename ... Args>
          void initialize_policy(Args &&... args) {
              _policy.initialize_policy(std::forward<Args>(args)...);
          }

          [[no_unique_address]]  Policy _policy;


          detached<_Policy> get_return_object() {
              return detached<_Policy>(this);
          }

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
