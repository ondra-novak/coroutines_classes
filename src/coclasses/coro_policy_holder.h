
#ifndef SRC_COCLASSES_CORO_POLICY_HOLDER_H_
#define SRC_COCLASSES_CORO_POLICY_HOLDER_H_

#include "resumption_policy.h"

namespace cocls {

template<typename _Policy = void>
class coro_policy_holder {
public:
    using Policy = std::conditional_t<std::is_void_v<_Policy>,typename resumption_policy::unspecified<void>::policy,_Policy>;


    [[no_unique_address]] Policy _policy;

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

    template<typename ... Args>
    bool initialize_policy(Args &&... args) {
        return _policy.initialize_policy(std::forward<Args>(args)...);
    }

};

}



#endif /* SRC_COCLASSES_CORO_POLICY_HOLDER_H_ */
