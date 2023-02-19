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
            auto h = std::coroutine_handle<promise_type>::from_promise(*this);
            if (h.done()) throw no_more_values_exception();
            return h;
        }


        //generate next item synchronously
        void next_sync() {
            //check whether generator is idle (we can't access busy generator)
            assert("Generator is busy" && _caller == nullptr);
            //resume generator now (_caller == nullptr)
            auto h = std::coroutine_handle<promise_type>::from_promise(*this);
            if (h.done()) throw no_more_values_exception();
            h.resume();
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
                auto h = std::coroutine_handle<promise_type>::from_promise(*this);
                if (h.done()) throw no_more_values_exception();
                h.resume();
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
    class [[nodiscard]] next_awt: public co_awaiter_policy_base<generator> {
    public:
        ///constructor - better call generator<>::next()
        next_awt(generator &owner):co_awaiter_policy_base<generator> (owner) {}

        ///if the state of generator is not known, generates next item and returns it state
        /**
         * @retval true next item or exception is available
         * @retval false next item is not avaiable
         */
        operator bool() const {
            if (_state) return true;
            if (this->_owner._promise->done()) return false;
            this->_owner._promise->next_sync();
            return await_resume();
        }

        ///if the state of generator is not known, generates next item and returns it state
        /**
         * @retval false next item is or exception available
         * @retval true next item is not avaiable
         */
        bool operator!() const {
            return !operator bool();

        }

        ///for co_await, determines, whether generator is done
        bool await_ready() const {
            return this->_owner._promise->done();
        }

        ///for co_await suspends coroutine and let the generator to generate next item
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            this->_h = h;
            return this->_owner._promise->next_async(this);
        }

        ///after generator is finished, retrieves status
        /**
         * @retval true next item or exception is available
         * @retval false next item is not avaiable
         */
        bool await_resume() const {
            _state = !this->_owner._promise->done();
            return _state;
        }

        ///subscribe other awaiter)
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


    ///Allow generator to be called
    /**
     * The generator can be called with zero or one argument depend on whether generator
     * requires argument or not.
     *
     * Result of call is future<Ret>. For generator with infinity cycle, you can
     * co_await the result or use .wait() to access value directly. If the
     * generator is limited, you receive the exception value_not_ready_exception()
     * after last item is generated. However you can co_await future::has_value() and then
     * convert future to bool to receive information whether the value is set. In case
     * that true is returned, you can use dereference (*) to access the result

     *
     * @param args argument of the generator (if enabled)
     * @return future with result
     *
     * @code
     * future<int> val = int_gen();
     * if (co_await val.has_value())
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
