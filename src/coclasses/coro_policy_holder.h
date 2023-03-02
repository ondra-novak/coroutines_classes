
#ifndef SRC_COCLASSES_CORO_POLICY_HOLDER_H_
#define SRC_COCLASSES_CORO_POLICY_HOLDER_H_

#include "resumption_policy.h"

namespace cocls {

template<typename _Policy = void>
class coro_policy_holder {
public:
    using Policy = std::conditional_t<std::is_void_v<_Policy>,typename resumption_policy::unspecified<void>::policy,_Policy>;


    [[no_unique_address]] Policy _policy;


    //this function allows to pass return value of the function without copying
    //it uses copy elision for C++17.
    //it expect a function, which returns constructed awaiter.
    //and depend on whether that awaiter supports set_resumption_policy will choose
    //what to do next
    template<typename Fn>
    auto set_resumption_policy_or_just_return(Fn&& awt) {
        //retrieve returned type
        using Ret = decltype(awt());
        //test whether object supports set_resumption_policy
        if constexpr (has_set_resumption_policy<Ret, Policy>) {
            //if does, let function constructs new awaiter
            return Ret::set_resumption_policy(awt(), _policy);
        } else {
            //otherwise pass return value to return value
            return awt();
        }
    }

    template<typename Awt>
    auto await_transform(Awt&& awt) noexcept {
        /* await_transform must construct object in return value.
         * By returning anything else (reference, rvalue reference), the
         * returned object is COPIED! As the result will probably
         * own object, operator co_await cannot be used on it, so
         * we need to resolve correct calling sequence and handle
         * operator co_await by own.
         */

        //so if the awaiter defines co_await()
          if constexpr (has_co_await<Awt>) {
              //handle object with set_resumption_policy
              return set_resumption_policy_or_just_return([&]{
                  //return - construct object returned by co_await awt
                  return awt.operator co_await();
              });
              //for global co_await
          } else if constexpr (has_global_co_await<Awt>) {
              return set_resumption_policy_or_just_return([&]{
                  //construct object by applying global co_await
                  return operator co_await(awt);
              });

              //if the awaiter directly has set_resumption_policy
          } else if constexpr (has_set_resumption_policy<Awt, Policy>) {
              //the function constructs the new object and that is returned
              return awt.set_resumption_policy(std::forward<Awt>(awt), _policy);
          } else {
              //if none of this, we must CONSTRUCT something here, we cannot copy
              //awaiter - could be noncopyable - so construct a wrapper and refer the awaiter in it.
              return awaiter_wrapper(awt);
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
