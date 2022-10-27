#pragma once
#ifndef SRC_COCLASSES_SRC_COCLASSES_AWAITER_TRANSFORM_H_
#define SRC_COCLASSES_SRC_COCLASSES_AWAITER_TRANSFORM_H_
#include <algorithm>
#include <type_traits>
#include "resumption_policy.h"

namespace cocls {

///Transforms awaiter - can transform result of the awaiter
/**
 * 
 * @tparam T result type
 * @tparam Awt awaiter to transform
 * @tparam Fn function to be called to transform result of the awaiter
 */
template<typename T, typename Awt, typename Fn>
class awaiter_transform: public std::remove_reference<Awt>::type {
public:
    using super_t = typename std::remove_reference<Awt>::type;
    awaiter_transform(Awt &&awt, Fn &&fn)
        :super_t(std::forward<Awt>(awt))
        ,_fn(std::forward<Fn>(fn)) {}
    
    T await_resume() {
        return _fn(super_t::await_resume());
    }
    
protected:
    Fn _fn;
};

///Transform result of awaitable expression by calling unary function
/**
 * 
 * co_await await_transform([](int x){return x*10;}, calculate_async())
 * 
 * @param fn
 * @param awt
 * @return transformed awaiter
 */
template<typename Fn, typename Awt>
auto await_transform(Fn &&fn, Awt &&awt) {
    if constexpr (has_co_await<Awt>::value) {
        auto a = awt.operator co_await();
        return await_transform([_awt = std::forward<Awt>(awt), _fn = std::forward<Fn>(fn)](auto &&x){
          return _fn(std::forward<decltype(x)>(x));  
        },std::move(a));    
    } else {
        return awaiter_transform<decltype(fn(awt.await_resume())),Awt, Fn>(std::forward<Awt>(awt), std::forward<Fn>(fn));    
    }
    
    
}



}



#endif /* SRC_COCLASSES_SRC_COCLASSES_AWAITER_TRANSFORM_H_ */
