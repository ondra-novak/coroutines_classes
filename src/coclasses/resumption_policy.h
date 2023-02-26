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

template<typename X>
inline constexpr bool has_co_await = !std::is_same_v<std::monostate, decltype(_details::test_has_co_await(std::declval<X>()))>;
template<typename X>
inline constexpr bool has_global_co_await = !std::is_same_v<std::monostate, decltype(_details::test_can_co_await(std::declval<X>()))> ;
template<typename X, typename Y>
inline constexpr bool has_set_resumption_policy = !std::is_same_v<std::monostate, decltype(_details::test_has_set_resumption_policy(std::declval<X>(), std::declval<Y>()))> ;
template<typename X>
inline constexpr bool has_initialize_policy = !std::is_same_v<std::monostate, decltype(_details::test_has_initialize_policy(std::declval<X>()))>;
template<typename X>
inline constexpr bool has_wait = !std::is_same_v<std::monostate, decltype(_details::test_has_wait(std::declval<X>()))>;
template<typename X, typename Y>
inline constexpr bool has_subscribe_awaiter = !std::is_same_v<std::monostate, decltype(_details::test_has_subscribe_awaiter(std::declval<X>(),std::declval<Y>()))>;
template<typename X>
inline constexpr bool has_join = !std::is_same_v<std::monostate, decltype(_details::test_has_join(std::declval<X>()))>;


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

    ///Determines, whether it is safe to block current thread
    /**
     * @retval true it is safe block current thread
     * @retval false there are scheduled coroutines, so current thread should
     * not be blocked. Blocking thread now can lead to deadlock.
     *
     * This function can be used by various by schedulers or managers where threads
     * are used to schedule coroutines. If one of coroutines need to block thread for
     * a while - for example, it needs to synchronously wait for a some event - it
     * can ask its policy whether it is safe to block the thread or not. If the
     * function returns true, it should should avoid synchronous (blocking) operations
     * and call co_await pause() to give other coroutines to run
     *
     */
    bool can_block() noexcept;

    ///Return next coroutine which is ready to be scheduled. This coroutine will be scheduled in current thread
    /**
     * @return coroutine to be schedule. Coroutine must be removed from the ready state, because caller
     * is going to resume this coroutine. If there is none such coroutine, function must return std::noop_coroutine()
     *
     */
    std::coroutine_handle<> resume_handle_next() noexcept;


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
        static constexpr std::coroutine_handle<> resume_handle(std::coroutine_handle<> h) noexcept {return h;}
        static constexpr bool can_block() {return true;}
        static std::coroutine_handle<> resume_handle_next() noexcept {return std::noop_coroutine();}
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
