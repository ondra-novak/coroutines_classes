/**
 * @file future.h
 */
#pragma once
#ifndef SRC_COCLASSES_FUTURE_H_
#define SRC_COCLASSES_FUTURE_H_

#include "awaiter.h"
#include "common.h"
#include "exceptions.h"
#include "with_allocator.h"


#include "poolalloc.h"
#include "coro_policy_holder.h"

#include <assert.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>



namespace cocls {

/*
 *     │                                │
 *     │owner──────────┐                │owner ──┐
 *     │               ▼                │        │
 *     │           ┌────────┐           ▼        │
 *     ▼           │        │      ┌─────────┐   │
 * co_await ◄──────┤ Future │◄─────┤ Promise │◄──┘
 *     │           │        │      └────┬────┘
 *     │           └────────┘           │
 *     │                                │
 *     │                                │
 *     ▼                                ▼
 */

///Future variable
/** Future variable is variable, which receives value in a future.
 *
 *  This class implements awaitable future variable. You can co_await on the variable,
 *  the coroutine is resumed, once the variable is set.
 *
 *  To set the variable, you need to retrieve a promise object as first. There can
 *  be only one promise object. It is movable only. Once the promise object is created,
 *  the future must be resolved before its destruction. The reason for such ordering is
 *  that neither future nor promise performs any kind of allocation. While future
 *  sits in coroutine's frame, the promise is just pointer which can be moved around,
 *  until it is used to set the value, which also invalidates the promise to prevent further
 *  attempts to set value.
 *
 *  Neither promise, nor future are MT Safe. Only guaranteed MT safety is for setting
 *  the value, which can be done from different thread, it also assumes, that value
 *  is not read before it is resolved.
 *
 *  The future itself is not movable nor copyable. To return future from a function, it
 *  must be constructed as return expression. The future can be constructed using
 *  a function which receives promise as argument. This function is called during
 *  construction of the future and can be used to initialize an asynchronous operation.
 *
 *  @code
 *  return future<int>([&](auto promise){
 *      begin_async(std::move(promise));
 *  });
 *  @endcode
 *
 *  You can also declare a coroutine of the type future_coro to construct the future with
 *  such a coroutine. The coroutine is started during construction of the future
 *
 *  @code
 *  future_coro<int> do_work() {...}
 *
 *  return future<int>(do_work());
 *  @endcode
 *
 *    Future can be awaited by multiple awaiters. However you need to ensure MT
 *  safety by proper synchronization. For example when there are multiple awaiters,
 *  ensure, that no awaiter wants to move the result outside of future. Also ensure,
 *  that future can't be destroyed after it is awaited. For multiple awaiting
 *  is recommended to use make_shared
 *
 *
 * @note Future can act as optional. If the promise is dropped without being called, the
 * future is set resolved, but without setting the value. In this case, accessing the
 * future cause the exception "value_not_ready_exception". However, you can detect this
 * situation before. If you store future in a variable, you can call has_value() which
 * can be also co_await-ed. The behavoir is the same as co_await the future itself,
 * but now, the result is true if the value has been set, or false if not.
 *
 */
template<typename T>
class future;
///Promise
/**
 * Promise is movable only object which serves as pointer to future to be set.
 * Its content is valid until the object is used to set the value, then it becomes
 * invalid. If this object is destroyed without setting the value of the associated
 * future, the future is resolved with the exception "await_canceled_exception"
 */
template<typename T>
class promise;



///Coroutine for run asynchronous operations, it can be used to construct future<>
/**
 * If some interface expect, that future<T> will be returned, you can
 * implement such future as coroutine. Just declare coroutine which
 * returns future_coro, this future object can be converted to future<T>
 *
 * @param T returned value, can be void
 * @param _Policy resumption policy - void means default policy
 */
template<typename T, typename _Policy = void>
class future_coro;

template<typename T, typename _Policy = void>
class future_coro_promise;


template<typename T>
class [[nodiscard]] future {
public:

    static constexpr bool is_future = true;

    using value_type = T;
    using reference = std::add_lvalue_reference_t<value_type>;
    using const_reference = std::add_lvalue_reference_t<std::add_const_t<value_type> >;
    static constexpr bool is_void = std::is_void_v<value_type>;
    using value_storage = std::conditional_t<is_void, int, value_type>;

    using promise_type = future_coro_promise<T, void>;

    enum class State {
        not_value,
        value,
        exception
    };

    class __SetValueTag {};
    class __SetExceptionTag {};
    class __SetNoValueTag {};

    ///construct empty future
    /**
     * It can be used manually. You need to obtain promise by calling the function get_promise(), then
     * future can be awaited
     */
    future():_awaiter(&empty_awaiter::instance) {};

    ///construct future, calls function with the promise
    /**
     * @param init function to start asynchronous operation and store the promise which is
     * eventually resolved.
     *
     * constructor can be used in return expresion. You can omit the constructor name itself as
     * the constructor is intentionally declared without explicit keyword
     */
    template<typename Fn>
    CXX20_REQUIRES(std::invocable<Fn, promise<T> >)
    future(Fn &&init) :_awaiter(nullptr) {
        init(promise<T>(*this));
    }

    template<typename Fn>
    CXX20_REQUIRES(std::same_as<decltype(std::declval<Fn>()()), future<T> >)
    future(Fn &&init) {
        new(this) future<T>(init());
    }

    template<typename ... Args>
    future(__SetValueTag, Args && ... args)
        :_value(std::forward<Args>(args)...)
        ,_awaiter(&empty_awaiter::disabled)
        ,_state(State::value){}

    future(__SetExceptionTag, std::exception_ptr e)
        :_exception(std::move(e))
        ,_awaiter(&empty_awaiter::disabled)
        ,_state(State::exception){}

    future(__SetNoValueTag)
        :_awaiter(&empty_awaiter::disabled)
        ,_state(State::not_value){}

    ///construct by future_coro - companion coroutine result
    /**
     * If you declare future_coro coroutine type, its result can be passed to the future.
     * @param coro coroutine
     *
     * it is also possible to use future<T> as coroutine (the function can use co_ keywords,
     * but note that future<> is not movable, but future_coro<> is movable
     *
     */
    template<typename _Policy>
    future(future_coro<T, _Policy> &&coro) {
       typename future_coro<T, _Policy>::promise_type &p = coro._h.promise();
       p._future = this;
       coro.resume_by_policy();
    }

    ///Resolves future by a value
    template<typename ... Args>
    static future<T> set_value(Args && ... args) {
        return future<T>(__SetValueTag(), std::forward<Args>(args)...);
    }

    ///Resolves future by an exception
    static future<T> set_exception(std::exception_ptr e) {
        return future<T>(__SetExceptionTag(), std::move(e));
    }

    ///Sets future to state not-value. The future is ready, but has no value
    static future<T> set_not_value() {
        return future<T>(__SetNoValueTag());
    }


    using awaiter = abstract_awaiter;

    ///retrieves promise from unitialized object
    promise<T> get_promise() {
        [[maybe_unused]] auto cur_awaiter = _awaiter.exchange(nullptr, std::memory_order_relaxed);
        assert("Invalid future state" && cur_awaiter== &empty_awaiter::instance);
        return promise<T>(*this);
    }

    ///construct future from result returned by a function (can be lambda)
    /**
     * @param fn function with zero argument and returns the same type
     *
     * @note future is destroyed and recreated. If an exception is thrown from the
     * function, the future is resolved with that exception
     */
    template<typename Fn>
    CXX20_REQUIRES(std::same_as<decltype(std::declval<Fn>()()), future<T> > )
    void result_of(Fn &&fn) noexcept {
        this->~future();
        try {
            new(this) future<T>(fn());
        } catch(...) {
            new(this) future<T>();
            get_promise()(std::current_exception());
        }
    }

    ///same as result_of
    template<typename Fn>
    CXX20_REQUIRES(std::same_as<decltype(std::declval<Fn>()()), future<T> > )
    future<T> &operator<<(Fn &&fn) noexcept {
        result_of(std::forward<Fn>(fn));
        return *this;
    }

    ///destructor
    ~future() {
        assert("Destroy of pending future" && !pending());
        switch (_state) {
            default:break;
            case State::value: _value.~value_storage();break;
            case State::exception: _exception.~exception_ptr();break;
        }
    }

    ///determines, whether future is initialized
    /**
     * @retval false the future has been created right now, there is no value, no
     * pending promise. You can destroy future, you can call get_promise()
     * @retval true the future is already initialized, it can have pending promise
     * or a can be already resolved.
     */
    bool initialized() const {
        return _awaiter.load(std::memory_order_relaxed) == &empty_awaiter::instance;
    }

    ///determines, whether future is pending. There is associated promise.
    /**
     * @retval true the future is pending, can't be destroyed. There is associated
     * promise, which points to the future
     * @retval false the future is either resolved or not yet initialized
     */
    bool pending() const  {
        auto s = _awaiter.load(std::memory_order_relaxed);
        return s != &empty_awaiter::instance && s!= &empty_awaiter::disabled;
    }

    ///determines, whether result is already available
    /**
     * @retval true result is available and can be retrieved. This includes state
     * when promise has been dropped, so the future has actually no value, but it
     * is in resolved state.
     * @retval false the future has no value, it could be not-initialize or pending
     */
    bool ready() const {
        return _awaiter.load(std::memory_order_acquire) == &empty_awaiter::disabled;
    }

    ///retrieves result value (as reference)
    /**
     * @return reference to the value, you can modify the value or move the value out.
     * Note if there are multiple awaiters, every awaiter can modify or move value. Keep
     * this in mind.
     *
     * @exception value_not_ready_exception accessing the future in not ready state
     * throws this exception. This exception can be thrown also if the future is
     * resolved, but with no value when promise has been dropped (as an broken promise)
     * @exception any if the future is in exceptional state, the stored exception is
     * thrown now
     *
     * @note accessing the value is not MT-Safe.
     */
    reference value() {
        switch (_state) {
            default: throw value_not_ready_exception();
            case State::exception: std::rethrow_exception(_exception);throw;
            case State::value:
                if constexpr(!is_void) return _value; else return ;
        }
    }

    ///retrieves result value (as reference)
    /**
     * @return const reference to the value.
     *
     * @exception value_not_ready_exception accessing the future in not ready state
     * throws this exception. This exception can be thrown also if the future is
     * resolved, but with no value when promise has been dropped (as an broken promise)
     * @exception any if the future is in exceptional state, the stored exception is
     * thrown now
     *
     * @note accessing the value is not MT-Safe.
     */
    const_reference value() const {
        switch (_state) {
            default: throw value_not_ready_exception();
            case State::exception: std::rethrow_exception(_exception);throw;
            case State::value:
                if constexpr(!is_void) return _value; else return ;
        }
    }


    ///retrieves result value (as reference). Waits synchronously if the value is not ready yet
    ///
    /**
     * @return reference to the value, you can modify the value or move the value out.
     * Note if there are multiple awaiters, every awaiter can modify or move value. Keep
     * this in mind.
     *
     * @exception value_not_ready_exception accessing the future in not ready state
     * throws this exception. This exception can be thrown also if the future is
     * resolved, but with no value when promise has been dropped (as an broken promise)
     * @exception any if the future is in exceptional state, the stored exception is
     * thrown now
     *
     * @note accessing the value is not MT-Safe.
     */
    reference wait() {
        return co_awaiter<future<T> >(*this).wait();
    }


    ///Same as wait()
    /**@see wait() */
    decltype(auto) join() {return wait();}



    ///Synchronize with future, but doesn't pick value or explore its state
    /**
     * Function waits synchronously until future is resolved, when continues. It
     * doesn't access the value, it doesn't thrown exception
     */
    void sync() const noexcept {
        co_awaiter<future<T> >(*const_cast<future<T> *>(this)).wait();
    }

    ///Wait asynchronously, return value
    /**
     * The operator retrieves awaiter which can be co_awaiter. The co_await operator
     * can return following:
     *
     * @return reference to the value, you can modify the value or move the value out.
     * Note if there are multiple awaiters, every awaiter can modify or move value. Keep
     * this in mind.
     *
     * @exception value_not_ready_exception accessing the future in not ready state
     * throws this exception. This exception can be thrown also if the future is
     * resolved, but with no value when promise has been dropped (as an broken promise)
     * @exception any if the future is in exceptional state, the stored exception is
     * thrown now
     *
     * @note accessing the value is not MT-Safe.
     */
    co_awaiter<future<T> > operator co_await() {return *this;}

    ///has_value() awaiter return by function has_value()
    class [[nodiscard]] has_value_awt: public co_awaiter_policy_base<future<T>> {
    public:
        has_value_awt(future<T> &owner):co_awaiter_policy_base<future<T> >(owner) {}
        bool await_ready() noexcept {return this->_owner.ready();}
        bool await_resume() noexcept {return this->_owner._state != State::not_value;}
        bool await_suspend(std::coroutine_handle<> h) {
            this->_h = h;
            return this->_owner.subscribe_awaiter(this);
        }
        operator bool() const {
            if (!this->_owner.ready()) this->_owner.sync();
            return this->_owner._state != State::not_value;
        }
    };


    ///Asks whether the future has value
    /**
     * @retval true future has value or exception
     * @retval false future has no value
     *
     * @note if called on pending future, it acts as wait(), so it blocks.
     *
     * @note function is awaitable. You can co_await has_value() which suspend
     * the coroutine until the value is ready
     */
    has_value_awt has_value() const {
        return has_value_awt(*const_cast<future<T> *>(this));
    }

    ///Asks whether the future has value
    /**
     * @retval true future has value or exception
     * @retval false future has no value
     *
     * @note if called on pending future, it acts as wait(), so it blocks
     * @see wait()
     */
    operator bool() const {
        return has_value();
    }

    ///Asks whether the future has value
    /**
     * @retval true future has value or exception
     * @retval false future has no value
     *
     * @note if called on pending future, it acts as wait(), so it blocks
     * @see wait()
     */
    bool operator!() const {
        return !has_value();
    }

    ///Dereference - acts as wait()
    /** @see wait() */
    reference operator *() {
        return wait();
    }




protected:
    friend class co_awaiter<future<T> >;
    friend class promise<T>;

    template<typename A, typename B>
    friend class future_coro;
    template<typename A, typename B>
    friend class future_coro_promise;


    union {
        value_storage _value;
        std::exception_ptr _exception;

    };
    mutable std::atomic<awaiter *> _awaiter = nullptr;
    State _state=State::not_value;

    //need for co_awaiter
    bool is_ready() const {return ready();}
    //need for co_awaiter
    bool subscribe_awaiter(abstract_awaiter *x) {return x->subscribe_check_ready(_awaiter, empty_awaiter::disabled);}
    //need for co_awaiter
    reference get_result() {return value();}

    template<typename ... Args>
    void set(Args && ... args) {
        assert("Future is ready, can't set value twice" && _state == State::not_value);
        if constexpr(!is_void) {
            new(&_value) value_type(std::forward<Args>(args)...);
        }
        _state = State::value;
    }

    void set(std::exception_ptr e) {
        assert("Future is ready, can't set value twice" && _state == State::not_value);
        new (&_exception) std::exception_ptr(std::move(e));
        _state = State::exception;
    }

    void resolve() {
        awaiter::resume_chain_set_ready(_awaiter, empty_awaiter::disabled, nullptr);
    }
    std::coroutine_handle<> resolve_resume() {
        auto n = std::noop_coroutine();
        awaiter *x = _awaiter.exchange(&empty_awaiter::disabled, std::memory_order_release);
        while (x != nullptr) {
            auto a = x;
            x = x->_next;
            a->_next = nullptr;
            auto h = a->resume_handle();
            if (h && h != n) {
                while (x != nullptr) {
                    auto a = x;
                    x = x->_next;
                    a->_next = nullptr;
                    a->resume();
                }
                return h;
            }
        }
        return n;
    }
};


///promise object - can be obtained by future<>::get_promise() or during construction of the future
/**
 * The promise object acts as function (invokable). It is movable, not copyable. If it is destroyed
 * without calling, the associated future is resolved with no value, which causes that exception is thrown.
 * The promise can be called concurrently where only first call is accepted, other are ignored.
 *
 * The promise is invokable as constructor of returned value with exception - the promise can be
 * called with std::exception_ptr which causes that future throws this exception.
 *
 * @tparam T
 */
template<typename T>
class promise {
public:
    ///construct empty promise - to be assigned
    promise():_owner(nullptr) {}
    ///construct promise pointing at specific future
    explicit promise(future<T> &fut):_owner(&fut) {}
    ///promise is not copyable
    promise(const promise &other) =delete;
    ///promise can be moved
    promise(promise &&other):_owner(other.claim()) {}
    ///destructor
    ~promise() {
        auto m = _owner.load(std::memory_order_relaxed);
        if (m) m->resolve();

    }
    ///promise cannot assignment by copying
    promise &operator=(const promise &other) = delete;
    ///promise can be assigned by move
    promise &operator=(promise &&other) {
        if (this != &other) {
            drop();
            _owner = other.claim();
        }
        return *this;
    }

    void drop() {
        auto m = claim();
        if (m) {
            m->resolve();
        }
    }

    ///construct the associated future
    /**
     * @param args arguments to construct the future's value - same as its constructor. For
     * promise<void> arguments are ignored
     * @retval true success (race won)
     * @retval false promise is already claimed (race lost)
     */
    template<typename ... Args>
    bool operator()(Args && ... args) {
        auto m = claim();
        if (m) {
            m->set(std::forward<Args>(args)...);
            m->resolve();
            return true;
        }
        return false;
    }

    ///construct the associated future
    /**
     * @param args arguments to construct the future's value - same as its constructor. For
     * promise<void> arguments are ignored
     * @retval true success (race won)
     * @retval false promise is already claimed (race lost)
     */
    template<typename ... Args>
    bool set_value(Args && ... args) {
        auto m = claim();
        if (m) {
            m->set(std::forward<Args>(args)...);
            m->resolve();
            return true;
        }
        return false;
    }

    ///Sets exception
    bool set_exception(std::exception_ptr e) {
        return set_value(e);
    }

    ///Returns true, if the promise is valid
    operator bool() const {
        return _owner != nullptr;
    }

    ///Returns true, if the promise is not valid
    bool operator !() const {
        return _owner == nullptr;
    }

    ///capture current exception
    bool unhandled_exception()  {
        return set_exception(std::current_exception());
    }

    ///claim this future as pointer to promise - used often internally
    future<T> *claim() const {
        return _owner.exchange(nullptr, std::memory_order_relaxed);
    }

    ///Retrieves promise identifier
    /** This helps to construct identifier, which can be used later to find promise
     * in some kind of containers. The empty promise's identifier is nullptr.
     *
     * @return identifier. Note the idenitifer is construct from pointer to associated
     * future. Do not cast the pointer to future to access the future directly. Use
     * claim() insteaD.
     *
     */
    const void *get_id() const {return _owner;}

    ///Bind arguments but don't resolve yet. Return function, which can
    /// be called to resolve the future
    template<typename ... Args>
    auto bind(Args &&... args) {
        return [p = std::move(*this),...args = std::forward<Args>(args)]{
            p(std::forward<Args>(args)...);
        };
    }

protected:

    ///to allows direct access from derived classes
    /** Sets the future value, but doesn't resolve */
    template<typename ... Args>
    static void set(future<T> *what,  Args && ... args) {
        what->set(std::forward<Args>(args)...);
    }

    ///to allows direct access from derived classes
    /** resolves future */
    static void resolve(future<T> *what) {
        what->resolve();
    }

    ///to allows direct access from derived classes
    /** resolves future from await_suspend or resume_handle*/
    static std::coroutine_handle<> resolve_resume(future<T> *what) {
        return what->resolve_resume();
    }

    mutable std::atomic<future<T> *>_owner;
};


///Promise with default value
/** If the promise is destroyed unresolved, the default value is set to the future */
template<typename T>
class promise_with_default: public promise<T> {
public:
    using super = promise<T>;
    using promise<T>::promise;

    template<typename ... Args>
    promise_with_default(super &&prom, Args &&... args)
        :promise<T>(std::move(prom)),def(std::forward<Args>(args)...) {}
    ~promise_with_default() {
        this->set_value(std::move(def));
    }
    promise_with_default(promise_with_default &&other) = default;
    promise_with_default &operator=(promise_with_default &&other) {
        if (this != &other) {
            promise<T>::operator=(std::move(other));
            def = std::move(def);
        }
        return *this;
    }
protected:
    T def;

};

///Promise with default value
/**If the promise is destroyed unresolved, the default value is set to the future
 *
 * @tparam T type, must be integral type
 * @tparam val default value
 */
template<typename T, T val>
class promise_with_default_v: public promise<T> {
public:
    using super = promise<T>;
    using promise<T>::promise;
    promise_with_default_v() = default;
    promise_with_default_v(promise_with_default_v &&other) = default;
    promise_with_default_v &operator=(promise_with_default_v &&other) = default;
    ~promise_with_default_v() {
        this->set_value(val);
    }
    promise_with_default_v(super &&p):promise<T>(std::move(p)) {}
};

///Promise with default value - constant is specified in template paramater
/**If the promise is destroyed unresolved, the default value is set to the future
 *
 * @tparam T type
 * @tparam val const pointer to default value, must have external linkage
 */
template<typename T, const T *val>
class promise_with_default_vp: public promise<T> {
public:
    using super = promise<T>;
    using promise<T>::promise;
    promise_with_default_vp() = default;
    promise_with_default_vp(promise_with_default_vp &&other) = default;
    promise_with_default_vp &operator=(promise_with_default_vp &&other) = default;
    ~promise_with_default_vp() {
        this->set_value(*val);
    }
    promise_with_default_vp(super &&p):promise<T>(std::move(p)) {}
};


///Futures with callback function
/**
 * When future is resolved a callback function i called
 * @tparam T type of value
 * @tparam Fn function type
 *
 * This class is intended to be used in classes as member variables, to avoid
 * memory allocation - because the future must be allocated somewhere.
 *
 * If you have no such place, use more convenient function make_promise
 *
 * @see make_promise()
 */
template<typename T, typename Fn>
class future_with_cb: public future<T>, public abstract_awaiter, public coro_promise_base {
public:

    ///Construct a future and pass a callback function
    future_with_cb(Fn &&fn):_fn(std::forward<Fn>(fn)) {
        this->_awaiter = this;
    }
    virtual void resume() noexcept override {
        _fn(*this);
        delete this;
    }
    promise<T> get_promise() {
        return promise<T>(*this);
    }

    virtual ~future_with_cb() = default;

protected:
    Fn _fn;

};


///Extends the future_with_cb with ability to be allocated in a storage
template<typename T, typename Storage, typename Fn>
using future_with_cb_no_alloc = custom_allocator_base<Storage, future_with_cb<T, Fn> >;


/**@{*/
///Makes callback promise
/**
 * Callback promise cause execution of the callback when promise is resolved.,
 * This function has no use in coroutines, but it can allows to use promises in normal
 * code. Result object is normal promise.
 *
 * There is also only one memory allocation for whole promise and the callback function.
 *
 * @tparam T type of promise
 * @param fn callback function. Once the promise is resolved, the callback function receives
 * whole future<T> object as argument (as reference). It can be used to retrieve the value from it
 *
 * @return promise<T> object
 *
 * @note callback is executed only after all instances of the promise are destroyed. This helps
 * to reduce side-effect which could potential happen during setting the value of the promise. So
 * execution is postponed. However ensure, that when promise is resolved, the promise instance
 * is being destroyed as soon as possible
 *
 * @see future<T>::get()
 */
template<typename T, typename Fn>
promise<T> make_promise(Fn &&fn) {
    auto f = new future_with_cb<T, Fn>(std::forward<Fn>(fn));
    return f->get_promise();
}


template<typename T, typename Fn, typename Storage>
promise<T> make_promise(Fn &&fn, Storage &storage) {
    auto f = new(storage) future_with_cb_no_alloc<T, Storage, Fn>(std::forward<Fn>(fn));
    return f->get_promise();
}
/**@}*/




template<typename T, typename _Policy>
class [[nodiscard]] future_coro {
public:

    friend class future<T>;

    using promise_type = future_coro_promise<T, _Policy>;

    future_coro(std::coroutine_handle<promise_type> h):_h(h) {}
    future_coro(future_coro &&other):_h(std::exchange(other._h, {})) {}

    ~future_coro() {
        if (_h) _h.destroy();
    }

    ///Starts the coroutine by converting it to future. Allows to initialize resumption policy
    /**
     * The function is equivalent to converting coroutine to the future<T> directly. However
     * this function allows you to initialize resumption policy by passing arguments
     *
     * @param args arguments passed to the initialize_policy
     * @return
     */
    template<typename ... Args>
    future<T> start(Args &&... args) {
        future_coro_promise<T, _Policy> &promise = _h.promise();
        promise.initialize_policy(std::forward<Args>(args)...);
        return future<T>(std::move(*this));
    }

    ///Detach coroutine
    /**
     * Allows to run coroutine detached. Coroutine is not connected
     * with any future variable, so result (and exception) is ignored
     */
    template<typename ... Args>
    void detach(Args &&... args) {
        future_coro_promise<T, _Policy> &promise = _h.promise();
        promise.initialize_policy(std::forward<Args>(args)...);
        resume_by_policy();
    }


protected:
    void resume_by_policy() {
        auto h = std::exchange(_h,{});
        future_coro_promise<T, _Policy> &promise = h.promise();
        promise._policy.resume(h);
    }


protected:
    std::coroutine_handle<promise_type> _h;
};

template<typename T, typename _Policy>
class future_coro_promise: public coro_promise_base, // @suppress("Miss copy constructor or assignment operator")
                    public coro_policy_holder<_Policy>,
                    public coro_unified_return<T, future_coro_promise<T,_Policy> > {
public:
    future<T> *_future = nullptr;


    future_coro<T, _Policy> get_return_object() {
        return std::coroutine_handle<future_coro_promise>::from_promise(*this);
    }
    struct final_awaiter: std::suspend_always {
        template<typename Prom>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Prom> me) noexcept {
            future_coro_promise &p = me.promise();
            future<T> *f = p._future;
            me.destroy();
            std::coroutine_handle<> h;
            std::coroutine_handle<> n = std::noop_coroutine();
            if (f && (h = f->resolve_resume()) != n) return h;
            return p._policy.resume_handle_next();
        }
    };

    std::suspend_always initial_suspend() noexcept {return {};}
    final_awaiter final_suspend() noexcept {return {};}

    template<typename ... Args>
    void resolve(Args && ... args) {
        if (_future) _future->set(std::forward<Args>(args)...);
    }
    void unhandled_exception() {
        if (_future) _future->set(std::current_exception());
    }
};

///Shared future works similar as future<> but can be moved or copied, because it is ref-counter shared place
/**
 * @tparam T type of value
 *
 * The instance of shared_future don't need to be returned from the function, as there is still
 * way how to convert return value from from future to shared_future.
 *
 * To construct such future, you need to pass function which returns the future<> to the constructor
 * of shared_future<>. You can use std::bind
 *
 * @code
 * std::shared_future f(std::bind(&do_work, a, b, c));
 * @endcode
 *
 * the above function calls do_work(a,b,c) and converts returned future to shared_future
 *
 * The shared future uses heap to allocate the shared state. It can be awaited by multiple
 * awaiters (each must holds its reference to the instance). It is also possible to
 * remove all reference before the future is marked ready. While the future is
 * pending, an extra reference is counted. Once the future is set ready, this reference
 * is removed and if it is last reference, the shared state is destroyed.
 *
 * Once the shared_future is ready, its content acts as ordinary variable. Anyone who
 * holds reference can read or even modify the content. There is no extra synchronization
 * for these actions, so they are not probably MT safe. This allows to move out the content
 * or read content by multiple readers.
 *
 *
 */
template<typename T, typename Base = cocls::future<T> >
class shared_future {


    class future_internal;

    class resolve_cb: public abstract_awaiter {
    public:
        void charge(std::shared_ptr<future_internal> ptr);
        virtual void resume() noexcept override {
            _ptr = nullptr;
        }
    protected:
        std::shared_ptr<Base> _ptr;
    };


    class future_internal: public Base {
    public:
        using future<T>::future;



        resolve_cb resolve_tracer;
    };

public:

    ///construct uninitialized object
    /**
     * Construct will not initialize the shared state because in most of cases
     * the future object is later replaced or assigned (which is allowed).
     *
     * So any copying of shared_future before initialization doesn't mean that
     * instance is shared.
     *
     * If you need to initialize the object, call init_if_needed() or get_promise()
     */
    shared_future() = default;

    ///Construct shared future retrieve promise
    /**
     * @param fn function which retrieve promise, similar to future() constructor.
     * Your future is initialized and your promise can be used to resolve the future
     *
     */
    template<typename Fn> CXX20_REQUIRES(std::invocable<Fn, promise<T> >)
    shared_future(Fn &&fn)
        :_ptr(std::make_shared<future_internal>(std::forward<Fn>(fn))) {

        _ptr->resolve_tracer.charge(_ptr);
    }


    ///Construct shared future from normal future
    /**
     * Only way, how to construct this future is to pass a function/lambda function or bound
     * function to the constructor, which is called during construction. Function must return
     * future<T>
     *
     * @param fn function to be called and return future<T>
     */
    template<typename Fn> CXX20_REQUIRES(std::same_as<decltype(std::declval<Fn>()()), Base > )
    shared_future(Fn &&fn)
        :_ptr(std::make_shared<future_internal>()) {
        _ptr->result_of(std::forward<Fn>(fn));
        if (_ptr->pending()) _ptr->resolve_tracer.charge(_ptr);
    }

    ///Construct shared future from future_coro
    /**
     * Starts coroutine and initializes shared_future. Result of coroutine is used to resolve
     * the future
     * @param coro coroutine result
     */
    template<typename _Policy>
    shared_future(future_coro<T, _Policy> &&coro)
        :_ptr(std::make_shared<future_internal>(std::move(coro))) {
        _ptr->resolve_tracer.charge(_ptr);
    }

    ///Retrieve the future itself
    /** retrieved as reference, can't be copied */
    operator Base &() {return *_ptr;};


    ///return resolved future
    template<typename ... Args>
    static shared_future<T> set_value(Args && ... args) {
        return shared_future([&]{return Base::set_value(std::forward<Args>(args)...);});
    }

    ///return resolved future
    static shared_future<T> set_exception(std::exception_ptr e) {
        return shared_future([&]{return Base::set_exception(std::move(e));});
    }

    ///initializes object if needed, otherwise does nothing
    void init_if_needed() {
        if (_ptr) _ptr = std::make_shared<future_internal>();
    }

    ///retrieves promise from unitialized shared_future.
    /**
     * Function initializes the future, then returns promise. You can retrieve only
     * one promise
     * @return promise object
     */
    auto get_promise() {
        init_if_needed();
        auto p = _ptr->get_promise();
        _ptr->resolve_tracer.charge(_ptr);
        return p;
    }

    ///Determines, whether future is ready
    bool ready() const {
        if (_ptr) return _ptr->ready();
        else return false;
    }

    ///Retrieves value
    decltype(auto) value() {
        if (_ptr) return _ptr->value();
        else throw value_not_ready_exception();;
    }

    ///Wait synchronously
    /**
     * @return the value of the future
     */
    decltype(auto) wait() {
        return _ptr->wait();
    }


    ///For compatible API - same as wait()
    decltype(auto) join() {
        _ptr->wait();
    }

    ///Synchronise with the future, but doesn't pick the value
    /** Just waits for result, but doesn't pick the result including the exception */
    void sync() {
        _ptr->sync();
    }

    ///co_await the result.
    auto operator co_await() {return _ptr->operator co_await();}

protected:
    std::shared_ptr<future_internal> _ptr;

};

template<typename T, typename P>
future(future_coro<T, P>) -> future<T>;


template<typename T, typename _Policy = void>
using async = future_coro<T, _Policy>;



template<typename T, typename Base>
inline void shared_future<T,Base>::resolve_cb::charge(std::shared_ptr<future_internal> ptr) {
       _ptr = ptr;
       if (!(ptr->operator co_await()).subscribe_awaiter(&ptr->resolve_tracer)) {
           _ptr = nullptr;
      }
}

///Construct a future with context attached to it
/** It can be returned from a function without need to worry, where context will be stored
 * So the whole implementation can be there without need to be initialized on stack
 *
 * @tparam T Type returned by this future (can be void)
 * @tparam Context Type of object constructed with the future. The constructor
 * receives extra argument (as first argument) containing promise to the future. The
 * constructor should store the promise and start asynchronous operation. Once
 * asynchronous operation is complete, the promise must be resolved. The template class
 * garanteed lifetime, it will not end before the promise is resolved. Ensure
 * that resolving the promise is last operation.
 */
template<typename T, typename Context>
class [[nodiscard]] future_with_context: public cocls::future<T> {
public:

    ///construct the object
    /**
     * @param args arguments passed to the constructor of Context.
     */
    template<typename ... Args>
    future_with_context(Args && ... args):
        cocls::future<T>([&](auto promise){
            new(&_ctx) Context(std::move(promise), std::forward<Args>(args)...);
    }),_constructed(true) {}
    ~future_with_context() {
        if (_constructed) _ctx.~Context();
    }

    ///construct / replace with return value of a function which is called (can be lambda)
    /**
     * @param fn function must return the same type. It is called without arguments. If exception
     * is thrown during call, the exception is captured and stored in future
     */
    template<typename Fn>
    CXX20_REQUIRES(std::same_as<decltype(std::declval<Fn>()()), future_with_context<T, Context> >)
    void result_of(Fn &&fn) noexcept {
        this->~future_with_context();
        try {
            new(this) future_with_context(fn());
        } catch (...) {
            new(this) future_with_context(std::monostate());
            throw;
        }
    }

    ///construct object uninitialized
    /**
     * result can be used as general future or can be initialized later using result_of()
     * @return uninitialized future
     */
    static future_with_context unitialized()  {
        return future_with_context(std::monostate());
    }

    template<typename ... Args>
    static future_with_context set_value(Args && ... args) {
        return future_with_context(std::monostate(), std::forward<Args>(args)...);

    }
    static future_with_context set_exception(std::exception_ptr e) {
        return future_with_context(std::monostate(), std::move(e));

    }

protected:
    future_with_context(std::monostate):_constructed(false) {}
    template<typename ... Args>
    future_with_context(std::monostate, Args && ... args)
        :future<T>(typename future<T>::__SetValueTag(), std::forward<Args>(args)...)
         ,_constructed(false) {}
    template<typename ... Args>
    future_with_context(std::monostate, std::exception_ptr e)
        :future<T>(typename future<T>::__SetExceptionTag(), std::move(e))
         ,_constructed(false) {}

    union {
        Context _ctx;
    };
    bool _constructed;
};

namespace _details {

template<typename T>
struct IsFuture {static constexpr bool value = false;};
template<typename T>
struct IsFuture<future<T> > {
    static constexpr bool value = true;
    using Type = T;
};
template<typename T, typename X>
struct IsFuture<future_with_context<T, X> > {
    static constexpr bool value = true;
    using Type = T;
};

template<typename T>
struct IsFuture<shared_future<T> > {
    static constexpr bool value = true;
    using Type = T;
};


template<typename FromFuture, typename Fn, bool dynamic>
class future_transform_context: public abstract_listening_awaiter<FromFuture>, public coro_promise_base {
public:
    using From = typename IsFuture<FromFuture>::Type;

    //To = fn(From);
    using To = decltype(std::declval<Fn>()(std::declval<From>()));

    template<typename Exec>
    future_transform_context(promise<To> prom, Fn &&fn, Exec &&exec)
        :_prom(std::move(prom))
        ,_fn(std::forward<Fn>(fn)) {
        this->await(std::forward<Exec>(exec));
    }

    using coro_promise_base::operator new;
    void *operator new(std::size_t,void *p) {
        return p;
    }


    virtual ~future_transform_context() = default;
protected:
    promise<To> _prom;
    Fn _fn;

    virtual void resume() noexcept override {
        try {
            if constexpr(std::is_void_v<To>) {
                if constexpr(std::is_void_v<From>) {
                    this->value();
                    _fn();
                } else {
                    _fn(this->value());
                }
                _prom();
            } else {
                if constexpr(std::is_void_v<From>) {
                    this->value();
                    _prom(_fn());
                } else {
                    _prom(_fn(this->value()));
                }
            }
        } catch (...) {
            _prom(std::current_exception());
        }
        if constexpr(dynamic) {
            delete this;
        }
    }

};


template<typename TransformFn, typename ExecFn>
using TransformRet = decltype(std::declval<TransformFn>()(
        std::declval<typename _details::IsFuture<decltype(std::declval<ExecFn>()())>::Type>()
));

}

///Transform future content - create inplace storage
/**
 * @param tfn transform function. The function accepts one argument, result of
 * future to be transformed. Function return transformed result
 * @param efn function to be executed which returning original future to be transformed
 *
 * @return transformed future - it is returned as derived class future_with_context
 *
 * @note function doesn't allocate memory
 */
template<typename TransformFn, typename ExecFn>
CXX20_REQUIRES(_details::IsFuture<decltype(std::declval<ExecFn>()())>::value)
auto transform_inline(TransformFn &&tfn, ExecFn &&efn) {

    using Ret = future_with_context<_details::TransformRet<TransformFn, ExecFn>,
            _details::future_transform_context<decltype(std::declval<ExecFn>()()), TransformFn, false>>;

    return Ret(std::forward<TransformFn>(tfn), std::forward<ExecFn>(efn));
}

///Transform future content
/**
 * @param tfn transform function. The function accepts one argument, result of
 * future to be transformed. Function return transformed result
 * @param efn function to be executed which returning original future to be transformed
 *
 * @return return future<T> object
 *
 * @note function allocates memory to store function temporary data required for transformation
 */
template<typename TransformFn, typename ExecFn>
CXX20_REQUIRES(_details::IsFuture<decltype(std::declval<ExecFn>()())>::value)
auto transform(TransformFn &&tfn, ExecFn &&efn) {
    using From = decltype(efn());
    using RetT = typename _details::IsFuture<From>::Type;
    using To = decltype(tfn(std::declval<RetT>()));
    return future<To>([&](auto p) {
        new _details::future_transform_context<From, TransformFn, true>(
                std::move(p), std::forward<TransformFn>(tfn), std::forward<ExecFn>(efn));
    });
}

///discard result of a future
/**
 * @param fn function which returns a future. Function is called. Return value
 * is discarded, so it no longer need to awaited.
 *
 * @note function allocates memory for result, which is destroyed when future is resolved
 */
template<typename Fn>
CXX20_REQUIRES(_details::IsFuture<decltype(std::declval<Fn>()())>::value)
void discard(Fn &&fn) {
    class Awt: public abstract_awaiter, public coro_promise_base {
    public:
        Awt(Fn &fn):_fut(fn()) {
            _waiting = (_fut.operator co_await()).subscribe_awaiter(this);
        }

        virtual void resume() noexcept override {
            delete this;
        }
        bool _waiting;
    protected:
        decltype(fn()) _fut;
    };

    auto x = new Awt(fn);
    if (!x->_waiting) x->resume();
}




}
#endif /* SRC_COCLASSES_FUTURE_H_ */

