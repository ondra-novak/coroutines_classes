#pragma once
#ifndef SRC_COCLASSES_GENERATOR_H_
#define SRC_COCLASSES_GENERATOR_H_
#include "awaiter.h"



#include "coro_storage.h"
#include "future.h"

#include "iterator.h"

#include <memory>
#include <functional>

namespace cocls {


///Generator
/**
 * Generator can be synchronous or asynchronous and can be accessed synchronously
 * and asynchronously. When asynchronous generator is accessed synchronously, then
 * thread which accessing such generator is blocked until the generator generates a value.
 * It is recommended to use asynchronous access for asynchronous generators.
 *
 * Generator is coroutine, which can use co_yield to emit value. It is also possible
 * to send values to the generator, because co_yield can return a value. If this
 * is enabled, it is possible to call co_yield nullptr to read the very first value.
 *
 * The generator can be controlled via iterator and can be used as container for range-for,
 * in all these cases, the generator is accessed synchronously.
 *
 * You can also use functions next() and value() to control the generator. The function
 * next() can be co_await-ed and it always return true - while new item is generated
 * and false, when no more items are generated. To access the result itself, you
 * need to use value()
 *
 * The generator can be also used as callable function, which returns future<>. The
 * future can act as optional, which allows to check, whether generator has new value or
 * not.
 *
 * The generator's coroutine is initialized suspended and it starts on the first access, this
 * is also true for the generator with the argument. To access the argument after the start,
 * the generator need to co_yield nullptr. The generator is full featured coroutine, it
 * can use co_await as it need. However any use of co_await activates asynchronous mode
 * of the generator, which can slightly reduce a performance especially when co_await
 * dosn't actually perform any asynchronous operation.
 *
 * @tparam Ret specifies return value of the call, or type of value, which the generator
 * generates. Can't be void, can't be std::nullptr. If you need to specify no-value, use
 * std::monostate
 * @tparam Arg specifies argument type. This type can be void, which means, that generator
 * doesn't expect any argument
 */
template<typename Ret, typename Arg = void>
class generator {
public:

    ///type of argument
    using arg_type = Arg;
    ///contains true, if the generator doesn't need argument
    static constexpr bool arg_is_void = std::is_void_v<Arg>;
    ///reference to argument or void
    using reference_Arg = std::add_lvalue_reference_t<Arg>;
    ///type of storage of arg, because void cannot be stored, it is stored as std::nullptr_t
    using storage_Arg_ptr = std::conditional_t<arg_is_void,std::nullptr_t, Arg *>;
    ///type of argument passed to a function, which defaults to std::nullptr_t in case, that Arg is void
    using param_Arg = std::conditional_t<arg_is_void,std::nullptr_t, reference_Arg>;

    ///type of abstract awaiter
    using awaiter = abstract_awaiter<false>;
    ///type of iterator
    using iterator = generator_iterator<generator<Ret, Arg> >;

    ///contains coroutine promise
    class promise_type: public coro_promise_base {

        class syncing_awaiter: public awaiter {
        public:
            std::atomic<bool> _flag = false;
            virtual void resume() noexcept override {
                _flag.store(true, std::memory_order_release);
                _flag.notify_all();
            }
            virtual std::coroutine_handle<> resume_handle() noexcept override {
                syncing_awaiter::resume();
                return std::noop_coroutine();
            }
        };

        class promise_awaiter: public promise<Ret>, public awaiter {
        public:
            promise_awaiter(promise<Ret> &&prom, promise_type *owner)
                :promise<Ret>(std::move(prom))
                ,_owner(owner) {}

            auto prepare_result() noexcept {
                //claim the promise, receive future pointer
                auto f = promise<Ret>::claim();
                //if generator is done, don't set value, just resolve without value (which throws exception on future)
                if (_owner->_done) return f;
                //if generator has exception, throw it
                else if (_owner->_exp) promise<Ret>::set(f, _owner->_exp);
                //if generator has value, set it
                else promise<Ret>::set(f, *_owner->_ret);
                //return future to continue
                return f;
            }

            virtual void resume() noexcept override {
                //first prepare result, keep prepared future on stack
                auto f = prepare_result();
                //destroy content of the object now, the space can be overwritten very soon
                this->~promise_awaiter();
                //resolve the future - this can cause that next cycle will began
                promise<Ret>::resolve(f);
            }
            virtual std::coroutine_handle<> resume_handle() noexcept override {
                //first prepare result, keep prepared future on stack
                auto f = prepare_result();
                //destroy content of the object now, the space can be overwritten very soon
                this->~promise_awaiter();
                //resolve and prepare for symmetric transfer to awaiting coroutine
                return promise<Ret>::resolve_resume(f);
            }

        protected:
            promise_type *_owner;
        };


        awaiter *_caller = {};
        [[no_unique_address]] storage_Arg_ptr _arg = {};
        Ret *_ret = {};
        std::exception_ptr _exp;
        bool _done = false;
        union {
            syncing_awaiter _sync_awt;
            promise_awaiter _promise_awt;
        };
    public:

        generator get_return_object() {
            return generator(this);
        }

        promise_type() {}
        ~promise_type() {}

        static constexpr std::suspend_always initial_suspend() noexcept {return {};}


        struct yield_suspend: std::suspend_always {
            promise_type *p = nullptr;
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> me) noexcept {
                p = &me.promise();
                p->_arg = nullptr;
                awaiter *caller = std::exchange(p->_caller, nullptr);
                return caller?caller->resume_handle():std::noop_coroutine();
            }
            reference_Arg  await_resume() noexcept {
                if constexpr(!arg_is_void) {
                    return *p->_arg;
                }
            }
        };

        struct yield_null: std::suspend_never { // @suppress("Miss copy constructor or assignment operator")
            promise_type *_p;
            yield_null(promise_type *p):_p(p) {};
            reference_Arg await_resume() noexcept {
                if constexpr(!arg_is_void) {
                    return *_p->_arg;
                }
            }
        };


        yield_suspend final_suspend() noexcept {
            _ret = nullptr;
            return {};
        }
        void unhandled_exception() {
            _exp = std::current_exception();
            _done = true;
        }
        void return_void() {
            _done = true;
        }
        yield_suspend yield_value(Ret &x) {
            _ret = &x;
            return {};
        }
        yield_suspend yield_value(Ret &&x) {
            _ret = &x;
            return {};
        }
        yield_null yield_value(std::nullptr_t) {
            return this;
        }


        void set_arg(param_Arg arg) {
            _arg = &arg;
        }

        std::coroutine_handle<> next_async(awaiter *caller) {
            assert("Generator is busy" && _caller == nullptr);
            _caller = caller;
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }


        //generate next item synchronously
        void next_sync() {
            //check whether generator is idle (we can't access busy generator)
            assert("Generator is busy" && _caller == nullptr);
            //resume generator now (_caller == nullptr)
            std::coroutine_handle<promise_type>::from_promise(*this).resume();
            //if generator sets the _caller instance
            //it is always syncing_awaiter
            //because the generator is asynchronous
            //this is handled in await_transform()
            if (_caller) {
                //retrieve pointer to syncing awaiter
                auto *ptr = static_cast<syncing_awaiter *>(_caller);
                //check that we have right pointer
                assert("Unexpected awaiter at this point" && ptr == &_sync_awt);
                //wait on flag
                ptr->_flag.wait(false, std::memory_order_relaxed);
                //destroy the awaiter
                ptr->~syncing_awaiter();
            }

        }

        //generate next item and prepare future
        future<Ret> next_future() {
            //check whether generator is idle (we can't access busy generator)
            assert("Generator is busy" && _caller == nullptr);
            //prepare future, retrieve promise
            return [&](auto promise) {
                //create new promise object and register it as _caller
                _caller = new(&_promise_awt) promise_awaiter(std::move(promise), this);
                //resume generator
                std::coroutine_handle<promise_type>::from_promise(*this).resume();
                //once generator is done, promise_awaiter is notified
            };
        }


        //when generator calls co_await, it is probably asynchronous
        //as we still don't know, whether the operation will continue asynchronously
        //but it definitely can
        template<typename Awt>
        decltype(auto) await_transform(Awt &&awt) {
            if (_caller == nullptr) {
                _caller = new(&_sync_awt) syncing_awaiter;
            }
            //because await_transform is defined, operator co_await is not used
            //so we need to replicate this behaviour
            if constexpr(has_co_await<Awt>::value) {
                return awt.operator co_await();
            } else if constexpr(has_global_co_await<Awt>::value) {
                return operator co_await(awt);
            } else {
                return std::forward<Awt>(awt);
            }
        }

        bool done() const {
            return _done;
        }

        const std::exception_ptr &exception() const {
            return _exp;
        }

        Ret *value() {
            return _ret;
        }

    };

    generator() = default;
    generator(promise_type *p):_promise(p) {}

    ///This type is returned as result of next();
    /**
     * The function next doesn't perform next step, it only returns this object.
     * You need to test result to bool() and for the first access, it actually
     * calls the generator. The reason for this is that you actually can co_await
     * the result, which performs asynchronous access to the generator. Result
     * of co_await is true or false depend on whether generator generated a new value
     */
    class next_awt: public co_awaiter_policy_base<generator> {
    public:
        next_awt(generator &owner):co_awaiter_policy_base<generator> (owner) {}
        ~next_awt() {
            operator bool();
        }

        operator bool() const {
            if (_state) return true;
            if (this->_owner._promise->done()) return false;
            this->_owner._promise->next_sync();
            return await_resume();
        }

        bool operator!() const {
            return !operator bool();

        }

        bool await_ready() const {
            return this->_owner._promise->done();
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            this->_h = h;
            return this->_owner._promise->next_async(this);
        }

        bool await_resume() const {
            _state = !this->_owner._promise->done() || !!this->_owner._promise->exception();
            return _state;
        }

        bool subscribe_awaiter(awaiter *awt) {
            this->_owner._promise->next_async(awt).resume();
            return true;
        }


    protected:
        mutable bool _state = false;
    };

    ///Retrieve ID of this coroutine
    coro_id get_id() {
        return std::coroutine_handle<promise_type>::from_promise(*_promise).address();
    }
    ///Returns iterator
    /**
     * Despite on name, this function loads the first item and returns iterator. Further calls
     * loads more and more items Actually there is no separate iterators. The iterator only
     * allows to iterate generated items through the ranged-for (simulation)
     *
     */
    iterator begin() {
        return iterator(*this, next());
    }
    ///Returns iterator which represents end of iteration
    iterator end() {
        return iterator(*this, false);
    }
    ///Runs generator and waits to generation of next item
    /**
     * @return co_awaitable object. If called from non-coroutine, you need to convert returned
     * object to bool to perform loading of next item and returning true/false whether the
     * next item is available
     *
     * @code
     * if (generator.next()) {
     *      //next item is loaded
     * } else {
     *      //generator is done
     * }
     * @endcode
     *
     * @code
     * if (co_await generator.next()) {
     *      //next item is loaded
     * } else {
     *      //generator is done
     * }
     * @endcode
     *
     * @param args the function requires zero on one argument, depend on
     * whethe Arg is void or not. If you pass rvalue reference ensure, that result
     * of this function is immediately used, otherwise, the passed value is destroyed
     * without reaching the generator itself - as it is carried as reference
     *
     *
     */

    template<typename ... Args>
    next_awt next(Args && ... args) {
        if constexpr(arg_is_void) {
            static_assert(sizeof...(args) == 0, "The generator doesn't expect an argument");
            return next_awt(*this);
        } else {
            static_assert(sizeof...(args) == 1, "The generator expects 1 argument");
            _promise->set_arg(args...);
            return next_awt(*this);
        }
    }

    ///Retrieves current value
    /**
     * @return current value
     *
     * @note you need to call next() or check for done()
     */
    Ret &value() {
        auto exp = _promise->exception();
        if (exp) [[unlikely]] std::rethrow_exception(exp);
        auto ret = _promise->value();
        if (ret) [[likely]] return *ret;
        throw value_not_ready_exception();
    }


    ///Allow generator to be callable
    /**
     * The generator can be called with zero or one argument depend on whether generator
     * requires argument or not.
     *
     * Result of call is future<Ret>. For generator with infinity cycle, you can
     * co_await the result or use .wait() to access value directly. If the
     * generator is not infinity, you receive the exception value_not_ready_exception()
     * after last item is generated. However you can co_await future::sync() and then
     * convert future to bool to receive information whether the value is set. In case
     * that true is returned, you can use dereference (*) to access the result

     *
     * @param args argument of the generator (if enabled)
     * @return future with result
     *
     * @code
     * future<int> val = int_gen();
     * co_await val.sync();
     * if (val) {
     *          std::cout << *val << std::endl;
     * } else {
     *          std::cout << "Done" << std::endl;
     * }
     * @endcode
     *
     * @b Tip - you can replace existing future with result of generator, using
     *    future<T>::result_of(generator)
     */
    template<typename ... Args>
    future<Ret> operator()(Args && ... args) {
        if constexpr(arg_is_void) {
            static_assert(sizeof...(args) == 0, "The generator doesn't expect an argument");
            return _promise->next_future();
        } else {
            static_assert(sizeof...(args) == 1, "The generator expects 1 argument");
            _promise->set_arg(args...);
            return _promise->next_future();
        }
    }

    ///returns true, if the generator is finished
    bool done() const {
        return _promise->done();
    }

protected:

    struct deleter {
        void operator()(promise_type *p) const noexcept {
            std::coroutine_handle<promise_type>::from_promise(*p).destroy();
        }
    };

    std::unique_ptr<promise_type, deleter> _promise;

};


#if 0

template<typename T>
class generator_promise;

///Generic generator
/**
 * @tparam T type to be generated
 *
 *
 * Generator supports both synchronous and asynchronous coroutine for generating,
 * and synchronous and asynchronous access from the reader
 *
 * - generating routine can use co_await
 * - the generator itself can be co_awaited
 *
 * Accessing generator synchronously while it itself running asynchronously is
 * provided via a condition variable, so accessing thread is blocked. It is
 * much better to use co_await to access asynchronous generator.
 *
 * It is possible to co_await synchronous generator, which uses symmetric transfer
 * between reading and generating coroutine
 *
 * It is possible to use ranged-for to read generated values, however, this
 * way is always synchronous.
 *
 * To asynchronously access the generator, you need to co_await result of method
 * next(), which returns true if there is next item, or false if generation is done
 *
 *
 * @code
 * generator<int> gen = ...;
 * while (co_await gen.next()) {
 *      int v = gen.value();
 * }
 * @endcode
 *
 * Above code can be used for synchronous access (without co_await)
 *
 * @code
 * generator<int> gen = ...;
 * while (gen.next()) {
 *      int v = gen.value();
 * }
 * @endcode
 *
 * For asynchronous access you can use next_cb to assign a callback which
 * is called when generation is complete. This feature is used by generator_aggregator
 *
 *

 */
template<typename T>
class generator {
public:

    using arg_type = void;

    using promise_type = generator_promise<T>;


    using iterator = generator_iterator<generator<T> >;

    ///Object returned by next() function
    /** it can be awaited by co_await, or accessed directly
     *
     * when accessed directly, it acts as boolean, which contains true
     * if there is next item, or false if not. Note accessing this
     * virtual value performs getting next item every time the value is
     * retrieved, So it is not recommended to store object anywhere, just
     * immediately convert that temporary object to bool
     *
     * @code
     *  while (gen.next()) { //synchronous access
     *          // work with gen.value()
     *  }
     * @endcode
     *
     * @code
     *  while (co_await gen.next()) {  //asynchronous access
     *          // work with gen.value()
     *  }
     * @endcode
     *
     *
     * */
    class next_awaiter: public abstract_awaiter<false> {
    public:
        next_awaiter(generator *owner):_owner(owner) {}
        next_awaiter(const next_awaiter &) = default;
        next_awaiter &operator=(const next_awaiter &) = default;


        ///retrieves whether next item is available
        /**
         * @retval true next item is available
         * @retval false next item is not available
         */
        operator bool() const {
            return _owner->_promise->next();
        }
        ///retrieves whether generator is done
        /**
         * @retval true generator done
         * @return false generator is not done yet
         */
        bool operator !() const {
            return !operator bool();
        }
        ///await this state


        bool await_ready() noexcept {
            return _owner->_promise->done();
        }

        bool await_resume() {
            return _owner->_promise->on_await_resume();
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            _h = h;
            return _owner->_promise->next_async(this);
        }

        void subscribe_awaiter(abstract_awaiter *awt) {
            _owner->_promise->next_async(awt).resume();
        }


    protected:
        generator *_owner;
        std::coroutine_handle<> _h;

        virtual void resume() noexcept override {
            _h.resume();
        }
        virtual std::coroutine_handle<void> resume_handle() override {
            return _h;
        }



    };
    class call_awaiter: public next_awaiter { // @suppress("Miss copy constructor or assignment operator")
    public:
        using next_awaiter::next_awaiter;
        operator bool() const {
            return _value || this->_owner->done() || fetch();
        }
        bool operator !() const {
            return !operator bool();
        }
        T *operator->() const {
            return *this?_value:nullptr;
        }
        T &operator *() const {
            return *(*this?_value:nullptr);
        }
        operator T *() const {
            return *this?_value:nullptr;;
        }
        bool await_ready() {
            return _value || this->_owner.done();
        }
        T *await_resume() {
            if (this->_owner->_promise->on_await_resume()) {
                return this->_owner._promise->get();
            } else {
                return nullptr;
            }
        }

    protected:
        mutable T *_value = nullptr;

        bool fetch() const {
            bool b = this->_owner->_promise->next();
            if (b) _value = this->_owner->_promise->get();
            return b;
        }
    };


    ///construct empty generator variable - can be assigned
    generator() = default;
    generator(promise_type *promise):_promise(promise) {}

    ///Retrieve ID of this coroutine
    coro_id get_id() {
        return std::coroutine_handle<promise_type>::from_promise(*_promise).address();
    }
    ///Returns iterator
    /**
     * Despite on name, this function loads the first item and returns iterator. Further calls
     * loads more and more items Actually there is no separate iterators. The iterator only
     * allows to iterate generated items through the ranged-for (simulation)
     *
     */
    iterator begin() {
        return iterator(*this, next());
    }
    ///Returns iterator which represents end of iteration
    iterator end() {
        return iterator(*this, false);
    }
    ///Runs generator and waits to generation of next item
    /**
     * @return co_awaitable object. If called from non-coroutine, you need to convert returned
     * object to bool to perform loading of next item and returning true/false whether the
     * next item is available
     *
     * @code
     * if (generator.next()) {
     *      //next item is loaded
     * } else {
     *      //generator is done
     * }
     * @endcode
     *
     */
    next_awaiter next() {
        return this;
    }
    ///Retrieves current value
    /**
     * @return current value
     *
     * @note you need to call next() or check for done()
     */
    T &value() {
        return *_promise->get();
    }
    ///Retrieves current value
    /**
     * @return current value
     *
     * @note you need to call next() or check for done()
     */
    const T &value() const {
        return *_promise->get();
    }



    ///Run generator and retrieve next item
    /**
     * @return returns pointer to next item or null, if there is no item available
     */
    call_awaiter operator()() {
        return this;
    }

    ///Checks, whether
    bool done() const {
        return _promise->done();
    }


protected:

    struct Deleter {
    public:
        void operator()(promise_type *p) {
            p->destroy();
        }
    };

    std::unique_ptr<promise_type, Deleter> _promise;

    promise_type *get_promise() const {return _promise.get();}
};

template<typename T>
class generator_promise: public coro_promise_base {
public:
    generator_promise() = default;
    generator_promise(const generator_promise &) = delete;
    generator_promise &operator=(const generator_promise &) = delete;

    using awaiter = abstract_awaiter<false>;

;

    void destroy() {
        auto h = std::coroutine_handle<generator_promise<T> >::from_promise(*this);
        h.destroy();
    }

    static std::suspend_always initial_suspend() noexcept {return {};}
    struct yield_suspender { // @suppress("Miss copy constructor or assignment operator")
        abstract_awaiter<> *_h;

        bool await_ready() const noexcept {return false;}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> ) const noexcept {
            return _h->resume_handle();
        }
        static constexpr void await_resume()  noexcept {};
    };

    static awaiter *load_awaiter(std::atomic<awaiter *> &a) {
        auto awt = a.exchange(nullptr, std::memory_order_release);
        a.notify_all();
        return awt;
    }

    yield_suspender final_suspend() noexcept {
        _value = nullptr;
        return yield_suspender{load_awaiter(_awaiter)};
    }

    void unhandled_exception() {
        _e= std::current_exception();
    }

    yield_suspender yield_value(T &value) noexcept {
        _value = &value;
        return yield_suspender{load_awaiter(_awaiter)};
    }
    yield_suspender yield_value(T &&value) noexcept {
        _value = &value;
        return yield_suspender{load_awaiter(_awaiter)};
    }

    generator<T> get_return_object() {
        return generator<T>(this);
    }

    void return_void() {}


    bool done() {
        auto h = std::coroutine_handle<generator_promise<T> >::from_promise(*this);
        return h.done();
    }

    bool on_await_resume()  {
        if (_e) std::rethrow_exception(_e);
        return !done();
    }

    bool next() {
        //use empty awaiter to resume when value is ready
        //because empty awaiter do nothing for resume
        [[maybe_unused]] auto chk = _awaiter.exchange(&empty_awaiter<false>::instance, std::memory_order_acquire);
        //previous awaiter must be cleared
        assert(chk == nullptr);
        //retrieve handle
        auto h = std::coroutine_handle<generator_promise<T> >::from_promise(*this);
        //if coroutine is already done - return false
        if (h.done()) return false;
        //this call block until result is generated or until generator is suspended
        h.resume();
        //ensure, that value is ready - synchronously wait
        _awaiter.wait(&empty_awaiter<false>::instance);
        //check for exception
        if (_e) std::rethrow_exception(_e);
        //return true, if value is non-null
        return _value != nullptr;
    }

    std::coroutine_handle<generator_promise<T> > next_async(awaiter *awt) {
        [[maybe_unused]] auto chk = _awaiter.exchange(awt, std::memory_order_acquire);
        assert(chk == nullptr);
        return std::coroutine_handle<generator_promise<T> >::from_promise(*this);
    }

    T *get() {return _value;}


protected:
    T *_value = nullptr;
    std::atomic<awaiter *>_awaiter = nullptr;
    std::exception_ptr _e;



};

///Construct generator which is able to recieve values through the co_yield
/**
 * @tparam Ret type of value passed to co_yield and returned by the generator
 * @tparam Arg type of value passed to the generator and returned by co_yield
 */
template<typename Ret, typename Arg>
class generator<Ret(Arg)> {
public:
    using value_type = Ret;

    using arg_type = Arg;

    using promise_type = generator_promise<Ret(Arg)>;

    ///Allows to ask for next item
    /**
     * works as awaiter but it also defines operator bool to perform
     * synchronous access - so asking whether there is next item executes
     * the generator
     */
    class next_awaiter: public abstract_awaiter<false> {
    public:
        next_awaiter(generator *owner, Arg &arg):_owner(owner),_args(arg) {}
        next_awaiter(const next_awaiter &) = default;
        next_awaiter &operator=(const next_awaiter &) = default;


        ///retrieves whether next item is available
        /**
         * @retval true next item is available
         * @retval false next item is not available
         */
        operator bool() const {
            return _owner->_promise->next();
        }
        ///retrieves whether generator is done
        /**
         * @retval true generator done
         * @return false generator is not done yet
         */
        bool operator !() const {
            return !operator bool();
        }
        ///await this state

        bool await_ready() noexcept {
            return _owner->_promise->done();
        }

        bool await_resume() {
            return _owner->_promise->on_await_resume();
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            _h = h;
            return _owner->_promise->next_async(this, _args);
        }

        void subscribe_awaiter(abstract_awaiter *awt) {
            _owner->_promise->next_async(awt,_args).resume();
        }
    protected:
        generator *_owner;
        std::coroutine_handle<> _h;
        Arg & _args;

        virtual void resume() noexcept override {
            _h.resume();
        }
        virtual std::coroutine_handle<void> resume_handle() override {
            return _h;
        }
    };

    ///Awaiter but also can act as a pointer to value
    /**
     * If object is used as pointer, it perform synchronous access.
     * Use co_await to perform asynchronous access
     */
    class call_awaiter: public next_awaiter { // @suppress("Miss copy constructor or assignment operator")
    public:
        using next_awaiter::next_awaiter;

        operator bool() const {
            return _value || this->_owner->done() || fetch();
        }
        bool operator !() const {
            return !operator bool();
        }
        Ret *operator->() const {
            return *this?_value:nullptr;
        }
        Ret &operator *() const {
            return *(*this?_value:nullptr);
        }
        operator Ret *() const {
            return *this?_value:nullptr;;
        }
        bool await_ready() {
            return _value || this->_owner->done();
        }
        Ret *await_resume() {
            if (this->_owner->_promise->on_await_resume()) {
                return this->_owner->_promise->get();
            } else {
                return nullptr;
            }
        }

    protected:
        mutable Ret *_value = nullptr;

        bool fetch() const {
            bool b = this->_owner->_promise->next(this->_args);
            if (b) _value = this->_owner->_promise->get();
            return b;
        }
    };

    ///construct empty generator variable - can be assigned
    generator() = default;
    generator(promise_type *promise):_promise(promise) {}

    ///Retrieve ID of this coroutine
    coro_id get_id() {
        return std::coroutine_handle<promise_type>::from_promise(*_promise).address();
    }
    next_awaiter next(Arg& arg) {
        return next_awaiter(this, arg);
    }
    next_awaiter next(Arg&& arg) {
        return next_awaiter(this, arg);
    }
    ///Retrieves current value
    /**
     * @return current value
     *
     * @note you need to call next() or check for done()
     */
    Ret &value() {
        return *_promise->get();
    }
    ///Retrieves current value
    /**
     * @return current value
     *
     * @note you need to call next() or check for done()
     */
    const Ret &value() const {
        return *_promise->get();
    }

    ///Run generator and retrieve next item
    /**
     * @param arg argument passed to the generator
     * @return hybrid object, which can be awaiter or can access to the next
     * item while item is retrieved on the first access if the value is converted
     *  to Ret *
     */
    call_awaiter /* Ret* */ operator()(Arg && arg) {
        return call_awaiter(this, arg);
    }

    ///Run generator and retrieve next item
    /**
     * @param arg argument passed to the generator
     * @return hybrid object, which can be awaiter or can access to the next
     * item while item is retrieved on the first access if the value is converted
     *  to Ret *
     */
    call_awaiter /* Ret* */operator()(Arg & arg) {
        return call_awaiter(this, arg);
    }

    ///Checks, whether generator is done
    bool done() const {
        return _promise->done();
    }



protected:

    struct Deleter {
    public:
        void operator()(promise_type *p) {
            p->destroy();
        }
    };

    std::unique_ptr<promise_type, Deleter> _promise;

    promise_type *get_promise() const {return _promise.get();}
};

template<typename Ret, typename Arg>
class generator_promise<Ret(Arg)>: public coro_promise_base {
public:
    generator_promise() = default;
    generator_promise(const generator_promise &) = delete;
    generator_promise &operator=(const generator_promise &) = delete;

    using awaiter = abstract_awaiter<false>;
    using argstuple = std::tuple<Arg>;

;

    void destroy() {
        auto h = std::coroutine_handle<generator_promise>::from_promise(*this);
        h.destroy();
    }

    static std::suspend_never initial_suspend() noexcept {return {};}
    struct yield_suspender { // @suppress("Miss copy constructor or assignment operator")
        abstract_awaiter<> *_h;
        Arg **_arg;

        bool await_ready() const noexcept {return false;}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> ) const noexcept {
            return _h->resume_handle();
        }
        Arg &await_resume()  noexcept {return **_arg;};
    };

    static awaiter *load_awaiter(std::atomic<awaiter *> &a) {
        auto awt = a.exchange(nullptr, std::memory_order_release);
        a.notify_all();
        return awt;
    }

    yield_suspender final_suspend() noexcept {
        _value = nullptr;
        return yield_suspender{load_awaiter(_awaiter), &_arg};
    }

    void unhandled_exception() {
        _e= std::current_exception();
    }

    yield_suspender yield_value(Ret &value) noexcept {
        _value = &value;
        return yield_suspender{load_awaiter(_awaiter), &_arg};
    }
    yield_suspender yield_value(Ret &&value) noexcept {
        _value = &value;
        return yield_suspender{load_awaiter(_awaiter), &_arg};
    }
    yield_suspender yield_value(std::nullptr_t) noexcept {
        _value = nullptr;
        return yield_suspender{load_awaiter(_awaiter), &_arg};
    }

    generator<Ret(Arg)> get_return_object() {
        return generator<Ret(Arg)>(this);
    }

    void return_void() {}


    bool done() {
        auto h = std::coroutine_handle<generator_promise>::from_promise(*this);
        return h.done();
    }

    bool on_await_resume()  {
        if (_e) std::rethrow_exception(_e);
        return !done();
    }

    bool next(Arg &arg) {
        //use empty awaiter to resume when value is ready
        //because empty awaiter do nothing for resume
        [[maybe_unused]] auto chk = _awaiter.exchange(&empty_awaiter<false>::instance, std::memory_order_acquire);
        //previous awaiter must be cleared
        assert(chk == nullptr);
        //retrieve handle
        auto h = std::coroutine_handle<generator_promise>::from_promise(*this);
        //if coroutine is already done - return false
        if (h.done()) return false;
        _arg = &arg;
        //this call block until result is generated or until generator is suspended
        h.resume();
        //ensure, that value is ready - synchronously wait
        _awaiter.wait(&empty_awaiter<false>::instance);
        //check for exception
        if (_e) std::rethrow_exception(_e);
        //return true, if value is non-null
        return _value != nullptr;
    }

    std::coroutine_handle<generator_promise> next_async(awaiter *awt, Arg &arg) {
        [[maybe_unused]] auto chk = _awaiter.exchange(awt, std::memory_order_acquire);
        assert(chk == nullptr);
        _arg = &arg;
        return std::coroutine_handle<generator_promise>::from_promise(*this);
    }

    Ret *get() {return _value;}


protected:
    Ret *_value = nullptr;
    Arg *_arg = nullptr;
    std::atomic<awaiter *>_awaiter = &empty_awaiter<false>::instance;
    std::exception_ptr _e;



};

#endif

///Not actual generator, just an interface
/**
 * @tparam T type of generator
 *
 * stoppable generator can be stopped anytime from outside. Result of stopping could
 * be marking generator finished.
 *
 * This is better solution than force destroying the generator, because you can't stop
 * it when it is pending.
 *
 * Using this interface is simple. The backend should create two functions. The first
 * function is actual generator while other function is function which starts the generator
 * passing a stopping singaling object into the generator. Then it pack generator
 * and stop function into instance of this object
 *
 */
template<typename T>
class stoppable_generator: public generator<T> {
public:
    using stop_fn = std::function<void()>;
    stop_fn stop;
    stoppable_generator(generator<T> &&gen, stop_fn &&fn)
        :generator<T>(std::move(gen))
        ,stop(std::move(fn)) {}
    stoppable_generator(stoppable_generator &&) = default;
    stoppable_generator(const stoppable_generator &) = delete;
};

}

#endif /* SRC_COCLASSES_GENERATOR2_H_ */
