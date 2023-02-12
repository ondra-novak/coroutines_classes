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
#include "resumption_policy.h"

#include <assert.h>
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


/*
 *     │                                                       ┌────────────────┐
 *     │owner──────────┐                         ┌─shared_ptr──┤ shared_promise │
 *     │               ▼                         │             └────────────────┘
 *     │           ┌────────┐       allocated    │
 *     ▼           │        │      ┌─────────┐   │             ┌────────────────┐
 * co_await ◄──────┤ future │◄─────┤ promise │◄──┼─shared_ptr──┤ shared_promise │
 *     │           │        │      │         │   │             └────────────────┘
 *     │           └────────┘      └─────────┘   │
 *     │                                         │             ┌────────────────┐
 *     │                                         └─shared_ptr──┤ shared_promise │
 *     ▼                                                       └────────────────┘
 */

///Coroutine for run asynchronous operations, it can be used to construct future<>
/**
 * If some interface expect, that future<T> will be returned, you can
 * implement such future as coroutine. Just declare coroutine which
 * returns async, this future object can be converted to future<T>
 *
 * @param T returned value, can be void
 * @param _Policy resumption policy - void means default policy
 */
template<typename T, typename _Policy = void>
class async;


template<typename T>
class future {
public:

    using value_type = T;
    using reference = std::add_lvalue_reference_t<value_type>;
    using const_reference = std::add_lvalue_reference_t<std::add_const_t<value_type> >;
    static constexpr bool is_void = std::is_void_v<value_type>;

    enum class State {
        not_ready,
        have_promise,
        value,
        exception
    };

    class __SetValueTag {};
    class __SetExceptionTag {};

    future() {};

    template<typename Fn> CXX20_REQUIRES(std::invocable<Fn, promise<T> >)
    future(Fn &&init) {
        init(get_promise());
    }
    template<typename ... Args>
    future(__SetValueTag, Args && ... args)
        :_value(std::forward<Args>(args)...)
        ,_awaiter(&empty_awaiter<true>::disabled)
        ,_state(State::value){}

    future(__SetExceptionTag, std::exception_ptr e)
        :_exception(std::move(e))
        ,_awaiter(&empty_awaiter<true>::disabled)
        ,_state(State::exception){}

    template<typename _Policy>
    future(async<T, _Policy> &&coro)
        :_state(State::have_promise)
    {
       typename async<T, _Policy>::promise_type &p = coro._h.promise();
       p._future = this;
       coro.detach();
    }

    template<typename ... Args>
    static future<T> set_value(Args && ... args) {
        return future<T>(__SetValueTag(), std::forward<Args>(args)...);
    }

    static future<T> set_exception(std::exception_ptr e) {
        return future<T>(__SetExceptionTag(), std::move(e));
    }

    using awaiter = abstract_awaiter<true>;

    promise<T> get_promise() {
        assert("Invalid future state" && _state == State::not_ready);
        _state = State::have_promise;
        return promise<T>(*this);
    }


    template<typename Fn>
    CXX20_REQUIRES(std::same_as<decltype(std::declval<Fn>()()), future<T> > )
    void result_of(Fn &&fn) {
        this->~future();
        try {
            new(this) future<T>(fn());
        } catch(...) {
            new(this) future<T>();
        }
    }


    ~future() {
        assert("Destroy of pending future" && _state != State::have_promise);
        switch (_state) {
            default:break;
            case State::value: _value.~value_storage();break;
            case State::exception: _exception.~exception_ptr();break;
        }
    }

    bool has_promise() const  {
        return _state == State::have_promise;
    }

    bool ready() const {
        return _awaiter.load(std::memory_order_acquire) == &empty_awaiter<true>::disabled;
    }

    reference value() {
        switch (_state) {
            default: throw value_not_ready_exception();
            case State::exception: std::rethrow_exception(_exception);throw;
            case State::value:
                if constexpr(!is_void) return _value; else return ;
        }
    }


    ///Wait synchronously
    /**
     * @return the value of the future
     */
    decltype(auto) wait() {
        return co_awaiter<future<T>,true >(*this).wait();
    }


    ///For compatible API - same as wait()
    decltype(auto) join() {return wait();}

    ///Synchronise with the future, but doesn't pick the value
    /** Just waits for result, but doesn't pick the result including the exception */
    void sync() {
        co_awaiter<future<T>,true >(*this).sync();
    }

    co_awaiter<future<T>,true> operator co_await() {return *this;}


protected:
    friend class co_awaiter<future<T>, true>;
    friend class promise<T>;

    template<typename A, typename B>
    friend class async;

    using value_storage = std::conditional_t<is_void, int, value_type>;

    union {
        value_storage _value;
        std::exception_ptr _exception;

    };
    mutable std::atomic<awaiter *> _awaiter = nullptr;
    State _state=State::not_ready;

    //need for co_awaiter
    bool is_ready() const {return ready();}
    //need for co_awaiter
    bool subscribe_awaiter(abstract_awaiter<true> *x) {return x->subscibre_check_ready(_awaiter);}
    //need for co_awaiter
    reference get_result() {return value();}

    template<typename ... Args>
    void set(Args && ... args) {
        assert("Future is ready, can't set value twice" && (_state == State::have_promise || _state == State::not_ready));
        if constexpr(!is_void) {
            new(&_value) value_type(std::forward<Args>(args)...);
        }
        _state = State::value;
    }

    void set(std::exception_ptr e) {
        assert("Future is ready, can't set value twice" && (_state == State::have_promise || _state == State::not_ready));
        new (&_exception) std::exception_ptr(std::move(e));
        _state = State::exception;
    }

    void resolve() {
        awaiter::resume_chain_set_disabled(_awaiter, nullptr);
    }
    std::coroutine_handle<> resolve_resume() {
        auto n = std::noop_coroutine();
        awaiter *x = _awaiter.exchange(&empty_awaiter<true>::disabled, std::memory_order_release);
        while (x != nullptr) {
            auto a = x;
            x = x->_next;
            auto h = a->resume_handle();
            if (h && h != n) {
                while (x != nullptr) {
                    auto a = x;
                    x = x->_next;
                    a->resume();
                }
                return h;
            }
        }
        return n;
    }
};

template<typename T>
class promise {
public:
    promise():_owner(nullptr) {}
    explicit promise(future<T> &fut):_owner(&fut) {}
    promise(const promise &other) =delete;
    promise(promise &&other):_owner(other.claim()) {}
    ~promise() {
        if (_owner.load(std::memory_order_relaxed))
            set_exception(std::make_exception_ptr(await_canceled_exception()));
    }
    promise &operator=(const promise &other) = delete;
    promise &operator=(promise &&other) {
        if (this != &other) {
            if (_owner) set_exception(std::make_exception_ptr(await_canceled_exception()));
            _owner = other.claim();
        }
        return *this;
    }


    template<typename ... Args>
    void operator()(Args && ... args) {
        auto m = claim();
        if (m) {
            m->set(std::forward<Args>(args)...);
            m->resolve();
        }
    }

    template<typename ... Args>
    void set_value(Args && ... args) {
        auto m = claim();
        if (m) {
            m->set(std::forward<Args>(args)...);
            m->resolve();
        }
    }

    void set_exception(std::exception_ptr e) {
        set_value(e);
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
    void unhandled_exception()  {
        set_exception(std::current_exception());
    }

    ///claim this future as pointer to promise - used often internally
    future<T> *claim() const {
        return _owner.exchange(nullptr, std::memory_order_relaxed);
    }


protected:

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
class future_with_cb: public future<T>, public abstract_awaiter<true>, public coro_promise_base {
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
class async {
public:

    friend class future<T>;

    class promise_type: public coro_promise_base, // @suppress("Miss copy constructor or assignment operator")
                        public coro_policy_holder<_Policy>,
                        public coro_unified_return<T, typename async<T,_Policy>::promise_type> {
    public:
        future<T> *_future = nullptr;

        async get_return_object() {
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }

        struct final_awaiter: std::suspend_always {
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> me) noexcept {
                promise_type &p = me.promise();
                future<T> *f = p._future;
                me.destroy();
                return f?f->resolve_resume():std::noop_coroutine();

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

    async(std::coroutine_handle<promise_type> h):_h(h) {}
    async(async &&other):_h(std::exchange(other._h, {})) {}

    ~async() {
        if (_h) _h.destroy();
    }

    ///Detach coroutine
    /**
     * Allows to run coroutine detached. Coroutine is not connected
     * with any future variable, so result (and exception) is ignored
     */
    void detach() {
        auto h = std::exchange(_h,{});
        h.resume();
    }


protected:
    std::coroutine_handle<promise_type> _h;
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
template<typename T>
class shared_future {


    class future_internal;

    class resolve_cb: public abstract_awaiter<true> {
    public:
        void charge(std::shared_ptr<future_internal> ptr);
        virtual void resume() noexcept override {
            _ptr = nullptr;
        }
    protected:
        std::shared_ptr<future<T> > _ptr;
    };


    class future_internal: public future<T> {
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
    template<typename Fn> CXX20_REQUIRES(std::same_as<decltype(std::declval<Fn>()()), future<T> > )
    shared_future(Fn &&fn)
        :_ptr(std::make_shared<future_internal>()) {
        _ptr->result_of(std::forward<Fn>(fn));
        if (_ptr->has_promise()) _ptr->resolve_tracer.charge(_ptr);
    }

    ///Construct shared future from async
    /**
     * Starts coroutine and initializes shared_future. Result of coroutine is used to resolve
     * the future
     * @param coro coroutine result
     */
    template<typename _Policy>
    shared_future(async<T, _Policy> &&coro)
        :_ptr(std::make_shared<future_internal>(std::move(coro))) {
        _ptr->resolve_tracer.charge(_ptr);
    }

    ///Retrieve the future itself
    /** retrieved as reference, can't be copied */
    operator future<T> &() {return *_ptr;};


    ///return resolved future
    template<typename ... Args>
    static shared_future<T> set_value(Args && ... args) {
        return shared_future([&]{return future<T>::set_value(std::forward<Args>(args)...);});
    }

    ///return resolved future
    static shared_future<T> set_exception(std::exception_ptr e) {
        return shared_future([&]{return future<T>::set_exception(std::move(e));});
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
future(async<T, P>) -> future<T>;




template<typename T>
inline void shared_future<T>::resolve_cb::charge(std::shared_ptr<future_internal> ptr) {
       _ptr = ptr;
       if (!(ptr->operator co_await()).subscribe_awaiter(&ptr->resolve_tracer)) {
           _ptr = nullptr;
      }
}

}
#endif /* SRC_COCLASSES_FUTURE_H_ */

