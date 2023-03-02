/** @file awaiter.h */

#ifndef SRC_COCLASSES_AWAITER_H_
#define SRC_COCLASSES_AWAITER_H_

#include "resumption_policy.h"
#include "queued_resumption_policy.h"

#include <algorithm>
#include <coroutine>
#include <atomic>
#include <cassert>
#include <memory>

namespace cocls {



///Abstract awaiter interface
class iawaiter_t {
public:

    ///called to resume coroutine
    /**When called, the coroutine must be scheduled for execution, immediately or by rules of the resumption policy */
    virtual void resume() noexcept = 0;
    ///called to retrieve coroutine handle for symmetric transfer
    /**When called, function must prepare handle of coroutine to be executed on current thread. When
     * the awaiter doesn't resume a coroutine, it can do anything what it want and then return
     * std::noop_coroutine. In most of cases, it call resume()
     */
    virtual std::coroutine_handle<> resume_handle() noexcept = 0;
    virtual ~iawaiter_t() = default;

};

///Abstract awaiter - extentends interface
/**
 * Can be chained - public variable *_next - NOTE: content of variable is ignored during destruction,
 * but it is used to chain awaiters to make waiting lists
 */
class abstract_awaiter: public iawaiter_t {
public:
    abstract_awaiter() = default;
    abstract_awaiter(const abstract_awaiter &)=default;
    abstract_awaiter &operator=(const abstract_awaiter &)=delete;

    void subscribe(std::atomic<abstract_awaiter *> &chain) {
        assert (this != chain.load(std::memory_order_relaxed));
        //release memory order because we need to other thread to see change of _next
        //this is last operation of this thread with awaiter
        while (!chain.compare_exchange_weak(_next, this, std::memory_order_release));

        assert (_next != this);
    }
    ///releases chain atomicaly
    /**
     * @param chain holds chain
     * @param skip awaiter to be skipped
     * @return count of released awaiters (including skipped)
     */
    static std::size_t resume_chain(std::atomic<abstract_awaiter *> &chain, abstract_awaiter *skip) {
        //acquire memory order, we need to see modifications made by other thread during registration
        //this is first operation of the thread of awaiters
        return resume_chain_lk(chain.exchange(nullptr, std::memory_order_acquire), skip);
    }

    ///Resume chain and marks its ready
    /**
     * @param chain chain to resume
     * @param ready_state state in meaning ready
     * @param skip awaiter to be skipped, can be nullptr
     * @return count of awaiters
     *
     * @note It marks chain disabled, so futher registration are rejected with false
     * @see subscribe_check_ready()
     */
    static std::size_t resume_chain_set_ready(std::atomic<abstract_awaiter *> &chain, abstract_awaiter &ready_state, abstract_awaiter *skip) {
        //acquire memory order, we need to see modifications made by other thread during registration
        //this is first operation of the thread of awaiters
        return resume_chain_lk(chain.exchange(&ready_state, std::memory_order_acquire), skip);
    }
    static std::size_t resume_chain_lk(abstract_awaiter *chain, abstract_awaiter *skip) {
        std::size_t n = 0;
        while (chain) {
            auto y = chain;
            chain = chain->_next;
            y->_next = nullptr;
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
    bool subscribe_check_ready(std::atomic<abstract_awaiter *> &chain, abstract_awaiter &ready_state) {
        assert(this->_next == nullptr);
        //release mode - because _next can change between tries
        while (!chain.compare_exchange_weak(_next, this, std::memory_order_release)) {
            if (_next == &ready_state) {
                _next = nullptr;
                //empty load, but enforce memory order acquire because this thread will
                //access to result
                chain.load(std::memory_order_acquire);
                return false;
            }
        }
        return true;

    }

    virtual std::coroutine_handle<> resume_handle() noexcept override {
        resume();
        return std::noop_coroutine();
    }

    abstract_awaiter *_next = nullptr;
};




///phony awaiter it is used to signal special value in awaiter's/chain
/** This awaiter doesn't resume anything */
class empty_awaiter: public abstract_awaiter {
public:

    ///Just instance for any usage
    static empty_awaiter instance;
    ///Disables awaiter's chain/slot. Any further registrations are impossible
    /** This allows to atomically replace awaiter with disabled, which can be
     * interpreted as "value is ready, no further waiting is required" while current
     * list of awaiters is picked and the awaiters are resumed
     *
     * @see abstract_awaiter::resume_chain_set_disabled
     */
    static empty_awaiter disabled;

    virtual void resume() noexcept override {}
    virtual std::coroutine_handle<> resume_handle() noexcept override {return std::noop_coroutine();}



};



inline empty_awaiter empty_awaiter::instance;
inline empty_awaiter empty_awaiter::disabled;


///Awaiter which carries and owner, base for many awaiters
template<typename promise_type>
class abstract_owned_awaiter: public abstract_awaiter {
public:
    abstract_owned_awaiter(promise_type &owner):_owner(owner) {}
    abstract_owned_awaiter(const abstract_owned_awaiter  &) = default;
    abstract_owned_awaiter &operator=(const abstract_owned_awaiter &) = delete;



protected:
    promise_type &_owner;

};



template<typename parent, typename policy>
class co_awaiter_policy;


///awaiter which supports change of resumption policy
template<typename promise_type>
class co_awaiter_policy_base: public abstract_owned_awaiter<promise_type> {
public:
    using abstract_owned_awaiter<promise_type>::abstract_owned_awaiter;

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
template<typename promise_type>
class [[nodiscard]] co_awaiter: public co_awaiter_policy_base<promise_type> {
public:
    using co_awaiter_policy_base<promise_type>::co_awaiter_policy_base;

    ///co_await related function
    bool await_ready() {
        assert("Double co_await attempt is invalid" && this->_next == nullptr);
        return this->_owner.is_ready();
    }
    ///co_await related function
    auto await_suspend(std::coroutine_handle<> h) {
        this->_h = h;
        return this->_owner.subscribe_awaiter(this);
    }
    ///co_await related function
    decltype(auto) await_resume(){
        assert("Coroutine resumed unexpectedly" && this->_next == nullptr);
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
    bool subscribe_awaiter(abstract_awaiter *awt) {
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
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        auto r = super::await_suspend(h);
        if constexpr(std::is_void_v<decltype(r)>) {
            return _p.resume_handle_next();
        } else if constexpr(std::is_same_v<bool, decltype(r)>) {
            if (r) return _p.resume_handle_next();
            else return h;
        } else {
            return r;
        }
    }
    decltype(auto) await_resume(){
        if (_resume_exception) {
            std::rethrow_exception(_resume_exception);
        }
        return super::await_resume();
    }

    virtual void resume() noexcept override  {
        try {
            assert("Attempt to resume still pending awaiter" && this->_next == nullptr);
            _p.resume(super::_h);
        } catch (...) {
            _resume_exception = std::current_exception();
            super::_h.resume();
        }
    }
    virtual std::coroutine_handle<> resume_handle() noexcept override {
        try {
            assert("Attempt to resume still pending awaiter" && this->_next == nullptr);
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


template<typename promise_type>
inline decltype(auto) co_awaiter<promise_type>::wait() {
    sync();
    return await_resume();
}

class sync_awaiter: public abstract_awaiter {
public:
    std::atomic<bool> flag = {false};

    virtual std::coroutine_handle<> resume_handle() noexcept override {
        sync_awaiter::resume();
        return std::noop_coroutine();
    }
    virtual void resume() noexcept override {
        flag.store(true);
        flag.notify_all();
    }
    void wait_sync() {
        flag.wait(false);
    }
};


template<typename promise_type>
inline void co_awaiter<promise_type>::sync() noexcept  {
    if (await_ready()) return ;
    sync_awaiter awt;
    if (subscribe_awaiter(&awt)) {
        awt.flag.wait(false);
    }

}


template<typename Awaitable>
inline decltype(auto) extract_awaiter(Awaitable &&awt) noexcept {
    if constexpr (has_co_await<Awaitable>) {
        auto x = awt.operator co_await();
        return x;
    } else if constexpr (has_global_co_await<Awaitable>) {
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
class abstract_listening_awaiter: public abstract_awaiter {
public:
    using super = abstract_awaiter;
    using awaiter_type = to_awaiter_type_t<Awaitable>;
    using value_type = decltype(std::declval<awaiter_type>().await_resume());
    static constexpr bool is_reference = std::is_reference_v<Awaitable>;

    static constexpr bool has_extra_awaiter = has_co_await<Awaitable> || has_global_co_await<Awaitable>;

    using AwaitableStorage = std::conditional_t<is_reference, std::add_pointer_t<std::decay_t<Awaitable> >, Awaitable>;

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
        construct_awt(std::forward<Fn>(fn));
        if constexpr(has_co_await<Awaitable>) {
            new(&_awt) awaiter_type(get_awt().operator co_await());
            handle_suspend(_awt);
        } else if constexpr(has_global_co_await<Awaitable>) {
            new(&_awt) awaiter_type(operator co_await(get_awt()));
            handle_suspend(_awt);
        } else {
            handle_suspend(get_awt());
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
        AwaitableStorage _awt_instance;
    };
    union {
        awaiter_type _awt;
    };

    template<typename Fn>
    void construct_awt(Fn &&fn) {
        if constexpr(is_reference) {
            Awaitable s = fn();
            new(&_awt_instance) AwaitableStorage(&s);
        } else {
            new(&_awt_instance) AwaitableStorage(fn());
        }
    }

    Awaitable &get_awt() {
        if constexpr(is_reference) {
            return *_awt_instance;
        } else {
            return _awt_instance;
        }
    }

    template<typename Awt>
    void handle_suspend(Awt &awt) {
        if (awt.await_ready()) {
            this->resume();
        } else {
            awt.subscribe_awaiter(this);
        }
    }

    void default_cleanup() {
        _awt_instance.~AwaitableStorage();
        if constexpr(has_extra_awaiter) {
            _awt.~awaiter_type();
        }
    }

};

///Helps to construct awaiter which calls a callback when future is resolved
/**
 * @tparam Awt type of returned future. The future or its awaitable must
 * support the function subscribe_awaiter;
 *
 * To construct the awaiter, use function prepare. Once you receive an object,
 * you can call await().
 *
 * The callback must be in two variants, or it must have auto argument, because it
 * can be called with either value or exception. You need to use if constexpr to
 * determine which case happened
 */
template<typename Awt>
class callback_awaiter {
public:


    ///static awaiter
    /**
     * You can construct this awaiter staticaly
     * @tparam Cb callback type
     */
    template<typename Cb>
    class awaiter: public  abstract_listening_awaiter<Awt> {
    public:

        awaiter(Cb &&fn):_cb(std::forward<Cb>(fn)) {}

    protected:
        virtual void resume() noexcept override {
            try {
                _cb(abstract_listening_awaiter<Awt>::value());
            } catch (...) {
                _cb(std::current_exception());
            }
        }

        Cb _cb;
    };
private:
    template<typename Cb>
    class dynamic: public awaiter<Cb> {
        using awaiter<Cb>::awaiter;
    protected:
        virtual void resume() noexcept override {
            awaiter<Cb>::resume();
            delete this;
        }
    };

    template<typename Cb, typename Allocator>
    class dynamic_alloc: public dynamic<Cb> {
    public:
        using dynamic<Cb>::dynamic;

        struct AllocHlp {
            Allocator &alloc;
            std::size_t sz;
        };

        void *operator new(std::size_t sz, AllocHlp &hlp) {
            hlp.sz = sz;
            return hlp.alloc(sz);
        }

        void operator delete(void *ptr, AllocHlp &hlp) {
            hlp.alloc.dealloc(ptr,hlp.sz);
        }

        void operator delete(void *ptr, std::size_t sz) {
            Allocator::dealloc(ptr, sz);
        }
    };

    using ptr_type = std::unique_ptr<abstract_listening_awaiter<Awt> >;
public:
    using value_type = typename abstract_listening_awaiter<Awt>::value_type;

    callback_awaiter() = default;
    ///construct callback awaiter
    /**
     * @param cb callback which is called when future is set ready. Note that the callback
     * must have two alternatives, one with the actual value and one with std::exception_ptr.
     * It can be also generic and you can use if constexpr(std::is_same_t<>) to detect
     * which case happened.
     *
     */
    template<typename Cb>
    CXX20_REQUIRES(std::invocable<Cb, value_type> && std::invocable<Cb, std::exception_ptr>)
    void prepare(Cb &&cb) {
        _ptr = std::make_unique<dynamic<Cb> >(std::forward<Cb>(cb));
    }
    ///construct callback awaiter allocated with an allocator
    /**
     * @param alloc reference to an allocator. Ensure that allocator's live isn't shorter
     * than callback's itself.
     *
     * @param cb callback which is called when future is set ready. Note that the callback
     * must have two alternatives, one with the actual value and one with std::exception_ptr.
     * It can be also generic and you can use if constexpr(std::is_same_t<>) to detect
     * which case happened.
     */
    template<typename Allocator, typename Cb>
    CXX20_REQUIRES((std::invocable<Cb, value_type> && std::invocable<Cb, std::exception_ptr>) && Storage<Allocator>)
    void prepare(Allocator &alloc, Cb &&cb) {
        dynamic_alloc<Cb, Allocator> hlp{alloc,0};
        _ptr = ptr_type(new(hlp) dynamic_alloc<Cb, Allocator>(std::forward<Cb>(cb)));
    }

    ///Awaits on an awaitable
    /**
     * calls the function passed as an argument and awaits on its return value. Once
     * the awaiter is set ready, the callback is called with the either value or
     * exception. Note that content of object is released by this function and the
     * object can be dropped or prepared again
     *
     * @param fn
     */
    template<typename Fn>
    CXX20_REQUIRES(std::same_as<decltype(std::declval<Fn>()()), Awt>)
    void await(Fn &&fn) {
        _ptr.release()->await(std::forward<Fn>(fn));
    }

    ///Directly construct the object
    /**
     * @see prepare
     * @param cb callback
     */
    template<typename Cb>
    CXX20_REQUIRES(std::invocable<Cb, value_type> && std::invocable<Cb, std::exception>)
    explicit callback_awaiter(Cb &&cb) {
        prepare(std::forward<Cb>(cb));
    }

    ///Directly construct the object
    /**
     * @see prepare
     * @param cb callback
     */
    template<typename Allocator, typename Cb>
    CXX20_REQUIRES((std::invocable<Cb, value_type> && std::invocable<Cb, std::exception>) && Storage<Allocator>)
    explicit callback_awaiter(Allocator &alloc, Cb &&cb) {
        prepare(std::forward<Cb>(cb));
    }

protected:
    ptr_type _ptr;

};


constexpr bool is_exception(std::exception_ptr &) {return true;}
template<typename Arg>
constexpr bool is_exception(Arg &) {return false;}


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
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        policy::resume(h);
        return policy::resume_handle_next();
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

///Wraps existing awaiter to new awaiter. Original awaiter is stored as reference
/**
 * @tparam Awt original awaiter
 *
 * This is used for await_transform, to await original awaiter copying. The
 * await_transform can return awaiter_wrapper instead original awaiter.
 */
template<typename Awt>
class awaiter_wrapper {
public:
    awaiter_wrapper (Awt &owner):_owner(owner) {}
    constexpr bool await_ready() {return _owner.await_ready();}
    constexpr auto await_suspend(std::coroutine_handle<> h) {return _owner.await_suspend(h);}
    constexpr decltype(auto) await_resume() {return _owner.await_resume();}
protected:
    Awt &_owner;
};

template<typename T>
awaiter_wrapper(T &) -> awaiter_wrapper<T>;




}
#endif /* SRC_COCLASSES_AWAITER_H_ */

