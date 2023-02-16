/** @file awaiter.h */

#ifndef SRC_COCLASSES_AWAITER_H_
#define SRC_COCLASSES_AWAITER_H_

#include "resumption_policy.h"
#include "queued_resumption_policy.h"

#include <algorithm>
#include <coroutine>
#include <atomic>

namespace cocls {

///Abstract awaiter
/**
 * allows to implement both co_awaiter and blocking_awaiter
 * @tparam promise_type type of promise (of task for example)
 * @tparam chain set this true (default false) to allow chains (includes next ptr to awaiter)
 *
 * The promise must implement
 *
 * bool is_ready(); - return true, if result is ready
 * bool subscribe_awaiter(abstract_awaiter *); - return true, if set, false if resolved
 * auto get_result(coroutines); - return result
 *
 */

class abstract_awaiter_base {
public:

    ///called to resume coroutine
    virtual void resume() noexcept = 0;

    ///called to retrieve coroutine handle for symmetric transfer
    virtual std::coroutine_handle<> resume_handle() {
        resume();
        return std::noop_coroutine();
    }
    virtual ~abstract_awaiter_base() = default;

};



template<bool chain = false>
class abstract_awaiter: public abstract_awaiter_base {
public:
    ///subscribe this awaiter but at the same time, check, whether it is marked ready
    /**
     * @param chain register to chain
     * @retval true registered
     * @retval false failed to register, object is already ready
     */
    bool subscibre_check_ready(std::atomic<abstract_awaiter *> &chain_);

    static constexpr bool chained = chain;
 };

///phony awaiter it is used to signal special value in awaiter's/chain
/** This awaiter doesn't resume anything */
template<bool chain>
class empty_awaiter: public abstract_awaiter<chain> {
public:

    ///Just instance for any usage
    static empty_awaiter<chain> instance;
    ///Disables awaiter's chain/slot. Any further registrations are impossible
    /** This allows to atomically replace awaiter with disabled, which can be
     * interpreted as "value is ready, no further waiting is required" while current
     * list of awaiters is picked and the awaiters are resumed
     *
     * @see abstract_awaiter<>::resume_chain_set_disabled
     */
    static empty_awaiter<chain> disabled;

    virtual void resume() noexcept {}



};

template<>
class abstract_awaiter<true>: public abstract_awaiter_base {
public:
    abstract_awaiter() = default;
    abstract_awaiter(const abstract_awaiter &)=default;
    abstract_awaiter &operator=(const abstract_awaiter &)=delete;

    void subscribe(std::atomic<abstract_awaiter *> &chain) {
        while (!chain.compare_exchange_weak(_next, this, std::memory_order_relaxed));
    }
    ///releases chain atomicaly
    /**
     * @param chain holds chain
     * @param skip awaiter to be skipped
     * @return count of released awaiters (including skipped)
     */
    static std::size_t resume_chain(std::atomic<abstract_awaiter *> &chain, abstract_awaiter *skip) {
        return resume_chain_lk(chain.exchange(nullptr), skip);
    }

    ///Resume chain and disable it
    /**
     * @param chain chain to resume
     * @param skip awaiter to be skipped, can be nullptr
     * @return count of awaiters
     *
     * @note It marks chain disabled, so futher registration are rejected with false
     * @see subscribe_check_ready()
     */
    static std::size_t resume_chain_set_disabled(std::atomic<abstract_awaiter *> &chain, abstract_awaiter *skip) {
        return resume_chain_lk(chain.exchange(&empty_awaiter<true>::disabled, std::memory_order_release), skip);
    }
    static std::size_t resume_chain_lk(abstract_awaiter *chain, abstract_awaiter *skip) {
        std::size_t n = 0;
        while (chain) {
            auto y = chain;
            chain = chain->_next;
            if (y != skip) y->resume();
            n++;
        }
        return n;
    }
    ///subscribe this awaiter but at the same time, check, whether it is marked ready
    /**
     * @note uses empty_awaiter<true>::disabled to mark whether the value is ready.
     *
     * @param chain register to chain
     * @retval true registered
     * @retval false registration unsuccessful, the object is already prepared
     */
    bool subscibre_check_ready(std::atomic<abstract_awaiter *> &chain);

    abstract_awaiter *_next = nullptr;
    static constexpr bool chained = true;
protected:
};


template<bool chain>
inline empty_awaiter<chain> empty_awaiter<chain>::instance;
template<bool chain>
inline empty_awaiter<chain> empty_awaiter<chain>::disabled;

///
template<typename promise_type, bool chain = false>
class abstract_owned_awaiter: public abstract_awaiter<chain> {
public:
    abstract_owned_awaiter(promise_type &owner):_owner(owner) {}
    abstract_owned_awaiter(const abstract_owned_awaiter  &) = default;
    abstract_owned_awaiter &operator=(const abstract_owned_awaiter &) = delete;



protected:
    promise_type &_owner;

};



template<typename parent, typename policy>
class co_awaiter_policy;

template<typename promise_type, bool chain = false>
class co_awaiter_policy_base: public abstract_owned_awaiter<promise_type, chain> {
public:
    using abstract_owned_awaiter<promise_type, chain>::abstract_owned_awaiter;

    ///Allows to change resumption policy.
    /** This member is called by a task, when await_transform, to supply
     * own policy. This method is static to able work well on derived awaiters
     *
     * @param _this reference to _this object, but in the most child type
     * @param p resumption policy
     * @return awaiter with policy
     */
    template<typename _This, typename _Policy>
    static co_awaiter_policy<_This, _Policy> set_resumption_policy(_This &&_this, _Policy &&p) {
        return co_awaiter_policy<_This, _Policy>(std::forward<_Policy>(p), std::forward<_This>(_this));
    }

#ifdef __CDT_PARSER__
    //this helps to Eclipse CDT parser to recognize co_await conversion
    using ReturnValue = decltype(std::declval<co_awaiter<promise_type,chain> >().await_resume());

    operator ReturnValue();
#endif
protected:
    std::coroutine_handle<> _h;

    virtual std::coroutine_handle<> resume_handle() noexcept  override {
        return _h;
    }
    virtual void resume() noexcept  override {
        resumption_policy::unspecified<void>::policy::resume(_h);
    }

    template<typename,typename> friend class co_awaiter_policy;

};


///Generic awaiter used in most object to handle co_await
template<typename promise_type, bool chain = false>
class [[nodiscard]] co_awaiter: public co_awaiter_policy_base<promise_type, chain> {
public:
    using co_awaiter_policy_base<promise_type, chain>::co_awaiter_policy_base;

    ///co_await related function
    bool await_ready() {
        return this->_owner.is_ready();
    }
    ///co_await related function
    auto await_suspend(std::coroutine_handle<> h) {
        this->_h = h;
        return this->_owner.subscribe_awaiter(this);
    }
    ///co_await related function
    decltype(auto) await_resume(){
        return this->_owner.get_result();
    }

    ///Wait synchronously
    /**
     * Blocks execution until awaiter is signaled
     * @return result of awaited operation
     */
    decltype(auto) wait();

    ///Wait synchronously
    /**
     * Blocks execution until awaiter is signaled
     * Doesn't pick neither result nor exception
     * Useful if you need to synchronize with awaiter without being
     * affected by the result - for example without need to handle exception
     */
    void sync() noexcept ;


    ///Subscribe custom awaiter
    /**
     * Allows to declare custom awaiter, which is resumed, when awaited result is ready.
     * @param awt reference to awaiter. You need to keep reference valid until it is called
     * @retval true registration done
     * @retval false awaiting expression is already resolved, so no registration done, you can
     * call await_resume()
     */
    bool subscribe_awaiter(abstract_awaiter<chain> *awt) {
        return this->_owner.subscribe_awaiter(awt);
    }


};

///Awaiter with policy
template<typename parent, typename policy>
class [[nodiscard]] co_awaiter_policy: public std::remove_reference<parent>::type {
public:
    using super = typename std::remove_reference<parent>::type;
    co_awaiter_policy(policy &&p, parent &&owner)
            :super(std::forward<parent>(owner))
            ,_p(std::forward<policy>(p)) {}


    bool await_ready() {
        return super::await_ready();
    }
    decltype(auto) await_suspend(std::coroutine_handle<> h) {
        return super::await_suspend(h);
    }
    decltype(auto) await_resume(){
        if (_resume_exception) {
            std::rethrow_exception(_resume_exception);
        }
        return super::await_resume();
    }

    virtual void resume() noexcept override  {
        try {
            _p.resume(super::_h);
        } catch (...) {
            _resume_exception = std::current_exception();
            super::_h.resume();
        }
    }
    virtual std::coroutine_handle<> resume_handle() noexcept override {
        try {
            return _p.resume_handle(super::_h);
        } catch (...) {
            _resume_exception = std::current_exception();
            super::_h.resume();
            return std::noop_coroutine();
        }
    }

    static co_awaiter_policy set_resumption_policy(co_awaiter_policy  &&_this, policy &&p) {
        return _this;
    }

protected:
    [[no_unique_address]] policy _p;
    std::exception_ptr _resume_exception;
};


template<typename promise_type, bool chain>
inline decltype(auto) co_awaiter<promise_type, chain>::wait() {
    sync();
    return await_resume();
}

template<typename promise_type, bool chain>
inline void co_awaiter<promise_type, chain>::sync() noexcept  {
    if (await_ready()) return ;

    class Awaiter: public abstract_awaiter<chain> {
    public:
        std::atomic<bool> flag = {false};
        virtual std::coroutine_handle<> resume_handle() override {
            Awaiter::resume();
            return std::noop_coroutine();
        }
        virtual void resume() noexcept override {
            flag.store(true);
            flag.notify_all();
        }
    };

    Awaiter awt;
    if (subscribe_awaiter(&awt)) {
        awt.flag.wait(false);
    }

}


template<bool chain>
inline bool abstract_awaiter<chain>::subscibre_check_ready(std::atomic<abstract_awaiter*> &chain_) {
    abstract_awaiter *n = nullptr;
    //register only if there is non-zero value
    //then result is ready, however if there is other awaiter, it is UB
    return chain_.compare_exchange_strong(n, this, std::memory_order_acquire);
}


inline bool abstract_awaiter<true>::subscibre_check_ready(std::atomic<abstract_awaiter<true> *>& chain_) {
    //try to put self to the top of chain - assume _next = nullptr
    while (!chain_.compare_exchange_strong(_next, this, std::memory_order_acquire)) {
        //failed because _chain != _next
        //see what is there (stored to _next)  - if it is ready or processed, we can't register
        if (_next == &empty_awaiter<true>::disabled) {
            //reset _next to be able detect it again
            _next = nullptr;
            //return false
            return false;
        }
    }
    //we successfully subscribed
    return true;
}

template<typename Awaitable>
inline decltype(auto) extract_awaiter(Awaitable &&awt) noexcept {
    if constexpr (has_co_await<Awaitable>::value) {
        auto x = awt.operator co_await();
        return x;
    } else if constexpr (has_global_co_await<Awaitable>::value) {
        auto x =operator co_await(awt);
        return x;
    } else {
        return std::forward<Awaitable>(awt);
    }
}


template<typename T>
using to_awaiter_type_t = decltype(extract_awaiter(std::declval<T>()));


///listening awaiter listen for signal about completion of operation
/**
 * Once operation is complete, it calls a virtual function resume. The
 * function can access value awailable during the event.
 *
 * @tparam Awaitable original awaitable object, for example future<X>,
 * task<X>, etc... You can also listen standard awaiter, for example mutex::lock()
 *
 * The awaiter must be derived from abstact_awaiter
 *
 * This is abstract class, you need to implement resume() function
 *
 * No heap allocation is performed in this class, any required
 * space is already reserved
 *
 */
template<typename Awaitable>
class abstract_listening_awaiter: public abstract_awaiter<to_awaiter_type_t<Awaitable>::chained> {
public:
    using super = abstract_awaiter<to_awaiter_type_t<Awaitable>::chained>;
    using awaiter_type = to_awaiter_type_t<Awaitable>;
    using value_type = decltype(std::declval<awaiter_type>().await_resume());

    static constexpr bool has_extra_awaiter = has_co_await<Awaitable>::value || has_global_co_await<Awaitable>::value;


    abstract_listening_awaiter() {};
    ~abstract_listening_awaiter() {
        cleanup();
    }
    abstract_listening_awaiter(const abstract_listening_awaiter &) = delete;
    abstract_listening_awaiter &operator=(const abstract_listening_awaiter &) = delete;

    ///Start awaiting
    /**
     * @param fn a function which is called and which result is awaited.
     * You cannot await on object passed as reference, it always must
     * be a function returning awaitable object
     *
     * Note that there can be only one awaiter being awaited at
     * the same time. The instance of this object must remain
     * valid until operation completes
     *
     * Once operation is complete, resume() is called. If the
     * operation was completed before, resume() is called immediately
     */
    template<typename Fn>
    void await(Fn &&fn) {
        cleanup();
        _need_cleanup = true;
        new(&_awt_instance) Awaitable(fn());
        if constexpr(has_co_await<Awaitable>::value) {
            new(&_awt) awaiter_type(_awt_instance.operator co_await());
            handle_suspend(_awt);
        } else if constexpr(has_global_co_await<Awaitable>::value) {
            new(&_awt) awaiter_type(operator co_await(_awt_instance));
            handle_suspend(_awt);
        } else {
            handle_suspend(_awt_instance);
        }
    }

    ///cleans up manually any localy stored awaiter or awaitable object
    /** function calls destructors on all temporary objects
     * and reclaims released space */
    void cleanup() {
        if (_need_cleanup) default_cleanup();
        _need_cleanup = false;
    }

    ///called when operation is complete
    /**
     * To receive a result of the operation, use the value() function
     * here
     */
    virtual void resume() noexcept override = 0;

    ///called when operation is complete in a coroutine
    /**
     * The main purpose is to respond with coroutine handle to be
     * called next. Default value calls resume() and returns
     * noop_coroutine.
     */
    virtual std::coroutine_handle<> resume_handle() noexcept override {
        resume();
        return std::noop_coroutine();
    }

    ///Retrieves result of asynchronous operation
    /**
     * @return result. It is in most cases returned as reference to
     * a variable which stores the result
     *
     * @note will throw stored exception. Because the resume() is
     * declared noexcept, you must wrap the code into try-catch block
     */
    value_type value() {
        if constexpr(has_extra_awaiter) {
            return _awt.await_resume();
        } else {
            return _awt_instance.await_resume();
        }
    }

protected:
    bool _need_cleanup = false;
    union {
        Awaitable _awt_instance;
    };
    union {
        awaiter_type _awt;
    };

    template<typename Awt>
    void handle_suspend(Awt &awt) {
        if (awt.await_ready()) {
            this->resume();
        } else {
            awt.subscribe_awaiter(this);
        }
    }

    void default_cleanup() {
        _awt_instance.~Awaitable();
        if constexpr(has_extra_awaiter) {
            _awt.~awaiter_type();
        }
    }

};


///Retrieves handle to currently running coroutine
/**
 * @code
 * std::coroutine_handle<> my_handle = co_await self();
 */
class self {
public:
    static bool await_ready() noexcept {return false;}
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        _h = h;
        return false;
    }
    std::coroutine_handle<> await_resume() {
        return _h;
    }

protected:
    std::coroutine_handle<> _h;
};

///Suspends current coroutine and immediate resumes it under different resumption policy
/**
 * @note "immediate resume" also depends on the policy. For example, if the coroutine is
 * resumed under dispatcher's policy, it is enqueued and resumed after the dispatcher finishes
 * the current task
 *
 * @tparam policy new policy. If you specify void, the current policy is used. This can be useful
 * for queued resumption policy, which causes that current coroutine is paused and other
 * queued coroutines can run (cooperative multitasking)
 */
template<typename policy = void>
class pause;

template<typename policy>
class pause: private policy {
public:
    template<typename ... Args>
    explicit pause(Args && ... args):policy(std::forward<Args>(args)...) {}

    static bool await_ready() noexcept {return false;}
    void await_suspend(std::coroutine_handle<> h) noexcept {
        policy::resume(h);
    }
    static void await_resume() noexcept {}

};

template<>
class pause<void> {
public:

    template<typename policy>
    static auto set_resumption_policy(pause<void> , policy &&p) {
        return pause<typename std::remove_reference<policy>::type>(std::forward<policy>(p));
    }

};




}
#endif /* SRC_COCLASSES_AWAITER_H_ */

