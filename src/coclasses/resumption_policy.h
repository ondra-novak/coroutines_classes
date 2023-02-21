/** @file resumption_policy.h
 *
 *  Resumption policy is policy about how each task is resumed.
 *
 *  Resumption policy can be specified on task, or on an awaiter. Generators has
 *  no resumption policy, they are always resumed on thread which calls them.
 *
 *  Resumption policy is enforced to an awaiter, if the awaiter supports function
 *  set_resumption_policy. Not all awaiters can support this function. For example
 *  awaiting on thread pool expects, that coroutine will be transfered to the thread
 *  pool with no exception. However this needs for awaiters that only sends signal, where
 *  resumption is side effect of such signal.
 *
 *  @see resumption_policy_concept
 *
 *
 *
 */
#ifndef SRC_COCLASSES_RESUMPTION_POLICY_H_
#define SRC_COCLASSES_RESUMPTION_POLICY_H_
#include "common.h"

#include <coroutine>
#include <variant>

namespace cocls {

namespace _details {

    template<typename X>
    auto test_has_co_await(X &&x) -> decltype(x.operator co_await());
    std::monostate test_has_co_await(...);

    template<typename X, typename Y>
    auto test_has_set_resumption_policy(X &&x, Y &&y) -> decltype(x.set_resumption_policy(std::forward<X>(x),std::forward<Y>(y)));
    std::monostate test_has_set_resumption_policy(...);

    template<typename X>
    auto test_has_initialize_policy(X &&x) -> decltype(&std::decay_t<X>::initialize_policy);
    std::monostate test_has_initialize_policy(...);

    template<typename X>
    auto test_has_wait(X &&x) -> decltype(x.wait());
    std::monostate test_has_wait(...);

    template<typename X, typename Y>
    auto test_has_subscribe_awaiter(X &&x, Y &&y) -> decltype(x.subscribe_awaiter(y));
    std::monostate test_has_subscribe_awaiter(...);

    template<typename X>
    auto test_has_join(X &&x) -> decltype(x.join());
    std::monostate test_has_join(...);

    template<typename X>
    auto test_can_co_await(X &&x) -> decltype(operator co_await(std::forward<X>(x)));
    std::monostate test_can_co_await(...);

}

///Determines whether specified awaiter object has operator co_await()
/**
 * @tparam X awaiter to test
 * @return value contains true, if the awaiter has such operator, or false if not
 */
template<typename X>
using has_co_await = std::negation<std::is_same<std::monostate, decltype(_details::test_has_co_await(std::declval<X>()))> >;
template<typename X>
using has_global_co_await = std::negation<std::is_same<std::monostate, decltype(_details::test_can_co_await(std::declval<X>()))> >;

///Determines whether specified awaiter object has set_resumption_policy() function
/**
 * @tparam X awaiter to test
 * @return value contains true, if there is such function, or false if not
 * @tparam Y policy object
 */
template<typename X, typename Y>
using has_set_resumption_policy = std::negation<std::is_same<std::monostate, decltype(_details::test_has_set_resumption_policy(std::declval<X>(), std::declval<Y>()))> >;

template<typename X>
using has_initialize_policy = std::negation<std::is_same<std::monostate, decltype(_details::test_has_initialize_policy(std::declval<X>()))> >;

template<typename X>
using has_wait = std::negation<std::is_same<std::monostate, decltype(_details::test_has_wait(std::declval<X>()))> >;
template<typename X, typename Y>
using has_subscribe_awaiter = std::negation<std::is_same<std::monostate, decltype(_details::test_has_subscribe_awaiter(std::declval<X>(),std::declval<Y>()))> >;
template<typename X>
using has_join = std::negation<std::is_same<std::monostate, decltype(_details::test_has_join(std::declval<X>()))> >;


///definition of various resumption policies
namespace resumption_policy {


///Resumption policy concept - template to create resumption policy
struct _policy_concept {

    ///Specifies how the coroutine is initially started. It is awaiter type.
    /**Its constructor must accept the resumption policy object */
    struct initial_awaiter;
    ///mandatory - handles resumption
    void resume(std::coroutine_handle<> h);
    ///mandatory - handles symmetric transfer
    /**
     * This called when symmetric transfer is possible. Coroutine is resumed when
     * other coroutine is suspended or finished. Function can return argument
     * to continue in symmetric transfer, or call resume() to perform standard resume.
     * In this case, it must return std::noop_coroutine as result;
     *
     * It is also possible to resume another coroutine which handle resumption of
     * a coroutine requested by argument
     *
     * @param h handle to be resumed
     * @return handle of coroutine to resume for symmetric transfer.
     */
    static std::coroutine_handle<> resume_handle(std::coroutine_handle<> h) noexcept;

    ///optional - allows to initialize the polici on a task
    /**
     * Function initializes policy object by specified arguments
     *
     * @retval true policy has been initialized. You can resume waiting coroutine now
     * @retval false policy has been initialized previously, or the initialization did
     * not changed state of running coroutine. The coroutine was not suspended by
     * policy before initialization, so you should not resume the handle you are holding
     *
     *
     * @note resumption rules in this case are because coroutine can use initial_awaiter
     * before policy is initialized. The return value hints, whether coroutine might be
     * suspended on this awaiter because the policy was not initialized. If the
     * return value is true, then such situation happened and caller need to resume its
     * coroutine. If return value is false, such situation did not happened and coroutine
     * is already running. Note that function is not responsible to determine actual state.
     * Its return value is determined from previous state of the policy. If there
     * were no suspension on the initial awaiter, the function can still return true.
     * In this case and it is about information known to the caller only, whether the
     * coroutine is awaiting on initial_awaiter or not (is suspended because other reason)
     *
     */
    bool initialize_policy(...);
};


///Awaiter concept to work with resumption policy
struct _awaiter_concept {

    ///await ready standard implementation
    bool await_ready();
    ///await suspend standard implementation
    bool await_suspend(std::coroutine_handle<> h);
    ///await resume standard implementation
    auto await_resume();

    ///creates a copy of awaiter with specified policy
    /**
     * Function is called from task<>::await_transform. It passes policy of the task
     * to enforce his policy. Result of the function is awaiter with given policy. Then
     * the awaiter must respect that resumption policy
     *
     * @param _this instance of this type or derived type. This allows to make wrapping awaiter
     * with top-most derived class
     *
     * @param p resumption policy
     *
     * @return new awaiter which respects given resumption policy
     */
    template<typename _This, typename Policy>
    static auto set_resumption_policy(_This _this,Policy p);

};

struct initial_suspend_never: public std::suspend_never {
    template<typename Policy> initial_suspend_never(Policy &p) {}
};

template<typename Policy>
struct initial_resume_by_policy: public std::suspend_always {
    Policy &_p;
    initial_resume_by_policy(Policy &p):_p(p) {}
    initial_resume_by_policy(const initial_resume_by_policy &p) = default;
    initial_resume_by_policy &operator=(const initial_resume_by_policy &p) = delete;
    constexpr bool await_ready() const noexcept {
        return _p.is_policy_ready();
    }
};




    ///Resumption of a coroutine is made recursive by calling resume on current stack frame
    /**
     * When coroutine is resumed, resumption is executed on current stack.
     * Execution is immediate
     */
    struct immediate {
        using initial_awaiter = initial_suspend_never;
        static void resume(std::coroutine_handle<> h) noexcept {h.resume();}
        static std::coroutine_handle<> resume_handle(std::coroutine_handle<> h) noexcept {return h;}
    };
    struct queued;
    struct parallel;
    struct thread_pool;

    ///when resumption policy is not specified
    /** This template can be overwritten by specializing to unspecified<void>
     *
     * Default resumption policy is start_immediately_then_queued. This
     * causes that coroutines acts as normal functions, only when a coroutine
     * suspended, then its resumption is handled through thread's queue
     *
     * */
    template<typename> struct unspecified {
        using policy = queued;
    };

}


}


#endif /* SRC_COCLASSES_RESUMPTION_POLICY_H_ */
