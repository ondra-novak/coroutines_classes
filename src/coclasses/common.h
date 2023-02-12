#pragma once
#ifndef SRC_COCLASSES_COMMON_H_
#define SRC_COCLASSES_COMMON_H_

#include <algorithm>
#include <concepts>


#ifdef __CDT_PARSER__
//This part is seen by Eclipse CDT Parser only
//eclipse doesn't support co_await and co_return, so let define some macros

//rewrite co_await to !, which correctly handles operator co_await -> operator! and co_await <expr> -> ! <expr>
#define co_await !
//rewrite co_return as throw
#define co_return throw
#define co_yield throw
#define consteval constexpr
#endif

//for compiler support concepts but not for CDT, which doesn't support the keyword 'requires'
#if defined( __cpp_concepts) and not defined (__CDT_PARSER__)
#define CXX20_REQUIRES(...) requires __VA_ARGS__
#else
#define CXX20_REQUIRES(...)
#endif

///Coroutine classes use this namespace
namespace cocls {

    ///Coroutine identifier.
    /**
     * For various purposes. For example scheduler<> uses it to cancel sleeping on
     * specified coroutine.
     */
    using coro_id = const void *;


    template<typename T, typename Derived>
    class coro_unified_return {
    public:
        template<typename X>
        CXX20_REQUIRES(std::constructible_from<T, X>)
        void return_value(X &&value) {
            static_cast<Derived *>(this)->resolve(std::forward<X>(value));
        }
    };

    template<typename Derived>
    class coro_unified_return<void, Derived> {
    public:
        void return_void() {
            static_cast<Derived *>(this)->resolve();
        }
    };

}


#endif /* SRC_COCLASSES_COMMON_H_ */
