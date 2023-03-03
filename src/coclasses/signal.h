
#ifndef SRC_COCLASSES_AWAITABLE_SLOT_H_
#define SRC_COCLASSES_AWAITABLE_SLOT_H_
#include "awaiter.h"
#include "queue.h"
#include "exceptions.h"
#include "function.h"

#include <atomic>
#include <coroutine>
#include <memory>
#include <variant>

namespace cocls {




/*
 * ┌─────────┐          ┌───────────┐     ┌──────────┐     ┌────────────┐               ┌────────────────────┐
 * │         │ co_await │           │     │  signal  │     │            │   fn(<param>) │                    │
 * │   coro  ├─────────►│  awaiter  │◄─┬──┤  shared  ├──┬─►│  receiver  │◄──────────────┤  signal generator  │
 * │         │          │           │  │  │  state   │  │  │            │               │                    │
 * └─────────┘          └───────────┘  │  └──────────┘  │  └────────────┘               └────────────────────┘
 *                                     │                │
 *                                     │                │
 *                              get_awaiter()      get_recever()
 */

///Awaitable signal
/**
 * Object with shared state which allows to broadcast a signal to awaiting coroutines.
 *
 * @code
 * cocls::task<> signal_example(publisher_type p) {
 *      cocls::signal<int>::awaiter awt;
 *      try {
 *          //subscribe to publisher which calls a callback for every value
 *          awt.listen([p](auto cb){
 *              //pass callback to publisher
 *              p.subscribe(std::move(cb));
 *          });
 *          while (true) {
 *              val = co_await awt;
 *              // process a value
 *          }
 *      } catch (cocls::await_canceled_exception &) {
 *          //done
 *      }
 * }
 * @endcode
 *
 * @code
 * cocls::task<> signal_example(cocls::signal::awaiter<int> awt) {
 *      try {
 *          while (true) {
 *              int val = co_await awt;
 *              // process value val
 *          }
 *      } catch (cocls::await_canceled_exception &) {
 *          //done
 *      }
 * }
 *
 * int main() {
 *      cocls::signal<int> sig;
 *      signal_example(sig.get_awaiter());
 *      auto rcv = sig.get_receiver();
 *      rcv(10);
 *      rcv(20);
 *      rcv(30);
 *  }
 * @endcode
 *
 *
 * @tparam T type of item, can be also void. If you need to pass multiple
 * items, declare it as struct or use tuple
 *
 * @note there should be no suspend between co_awaits, othewise, the suspended
 * coroutine can miss an update. The registration to next value is done by
 * calling co_await on the awaiter. Right after co_await returns, the publisher
 *  (caller) is suspended until the resumed coroutine is suspended again regadless
 *  on reason of suspension, so if the coroutine is suspended on asynchronous operation,
 *  the caller is resumed and can deliver a new item while the coroutine is still processing
 *  asynchrnous operation and so it is not registered yet. If you need such
 *  feature, consider using a queue.
 */

template<typename T>
class signal {

    using storage_type = std::conditional_t<std::is_void_v<T>, bool , T>;

    struct state { // @suppress("Miss copy constructor or assignment operator")
        std::atomic<abstract_awaiter *> _chain;
        storage_type *_cur_val = nullptr;

        ~state() {
            _cur_val = nullptr;
            abstract_awaiter::resume_chain(_chain,nullptr);
        }

    };


public:

    using value_type = T;
    using reference = std::add_lvalue_reference_t<T>;
    using rvalue_reference = std::add_rvalue_reference_t<T>;
    using base_type = std::conditional_t<std::is_void_v<T>, bool ,std::remove_cvref<T> >;

    using rvalue_param = std::conditional_t<std::is_void_v<T>,bool &&,rvalue_reference>;
    using lvalue_param = std::conditional_t<std::is_void_v<T>,bool &,reference>;

    ///signal receiver
    /** used to receive signals from signal generator
     * it is expected that the signal generator generates signals as calls of
     *  a function. This object acts as function, which can receive
     */
    class receiver {
    public:

        ///create awaitable slot
        receiver(std::shared_ptr<state> state):_state(std::move(state)) {}


        ///wake up all awaiters and pass value constructed by arguments (passed to the constructor)
        /**
         * @param args arguments used to construct value (using constructor). The
         * value is destroyed before return.
         *
         * @note the function returns after all awaiting coroutines processed the value
         *
         * @note the function is not MT-Safe, use proper synchronization to achieve mt-safety
         */
        template<typename ... Args>
        CXX20_REQUIRES(std::is_constructible_v<storage_type, Args...> )
        void operator()(Args && ... args) const {
            base_type v(std::forward<Args>(args)...);
            _state->_cur_val = &v;
            abstract_awaiter::resume_chain(_state->_chain,nullptr);
        }

        ///wake up all awaiters and pass value as rvalue reference
        /**
         * @param val value to broadcast to all awaiters
         *
         * @note the function returns after all awaiting coroutines processed the value
         *
         * @note the function is not MT-Safe, use proper synchronization to achieve mt-safety
         *
         */
        void operator()(rvalue_param val) const {
            _state->_cur_val = &val;
            abstract_awaiter::resume_chain(_state->_chain,nullptr);
        }

        ///wake up all awaiters and pass value as lvalue reference
        /**
         * @param val value to broadcast to all awaiters
         *
         * @note the function returns after all awaiting coroutines processed the value
         *
         * @note the function is not MT-Safe, use proper synchronization to achieve mt-safety
         *
         */
        void operator()(lvalue_param val) const {
            _state->_cur_val = &val;
            abstract_awaiter::resume_chain(_state->_chain,nullptr);
        }

        auto get_signal() {

        }

    public:
        std::shared_ptr<state> _state;

    };

    ///awaiter object - you can co_await on it
    class awaiter: public abstract_awaiter {
    public:
        ///create empty awaiter
        /** if such awaiter is co_awaited, it always throws exception
         * await_canceled_exception()
         *
         * You can use empty awaiter to assign the instance later, or
         * you can call create_signal, to create signal slot and initialize awaiter
         * at the same point
         *
         */
        awaiter() = default;
        ///copy operator just shares the state, but not internal state
        awaiter(std::weak_ptr<state> state):_wk_state(std::move(state)) {}
        ///move operator moves everything
        awaiter(const awaiter &x):_wk_state(x._wk_state) {}
        awaiter(awaiter &&x) = default;
        awaiter &operator=(const awaiter &x) {
            if (&x != this) {
                _wk_state = x._wk_state;
            }
            return *this;
        }
        awaiter &operator=(awaiter &&) = default;

        ///required for co_await
        static constexpr bool await_ready() noexcept {
            return false;
        }
        ///required for co_await
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            auto s = _wk_state.lock();
            if (s) {
                _h = h;
                this->subscribe(s->_chain);
                return true;
            }  else {
                if (_reg) {
                    signal sig;
                    _wk_state = sig._state;
                    _h = h;
                     this->subscribe(sig._state->_chain);
                    _reg(sig.get_receiver());
                    return true;
                }
                return false;
            }
        }

        ///required for co_await
        reference await_resume() {
            auto s = _wk_state.lock();
            if (s) {
                auto v = s->_cur_val;
                if (v) {
                    if constexpr(std::is_void_v<T>) {
                        return;
                    } else {
                        return *v;
                    }
                }
            }
            throw await_canceled_exception();
        }
        ///allows to subscribe the awaiter which is later resume through function resume()
        /**
         * @param awt awaiter to subscribe
         * @retval true registered
         * @retval false not registered, you can continue and call await_resume to receive
         * the value
         *
         */
        bool subscribe_awaiter(abstract_awaiter *awt) {
            auto s = _wk_state.lock();
            if (!s) return false;
            awt->subscribe(s->_chain);
            return true;
        }

        ///Listen for signals
        /**
         * Function expects a lambda function, which is called with the awaitable_signal<> instance
         * which can be stored as std::function, or other callable.
         *
         * @param fn function to be called to register a signal receiver to a signal generator. An
         *      instance of the signal::receiver is passed as an argument. This function is not called
         *      immediately! so you can't use [&] in the lambda function. The function is
         *      called on very first co_await for this awaiter. This feature allows to atomically
         *      register and await for signals. When this function is called, the coroutine
         *      is already suspended and listening for signals.
         *
         * @note the function clears internal state of the awaiter, which means, that the awaiter
         * will no longer to listen previously registered signal generator.
         *
         */
        template<typename Fn>
        CXX20_REQUIRES(std::invocable<Fn, signal<T>::receiver >)
        void listen(Fn &&fn) {
            _wk_state = std::weak_ptr<signal<T>::state >();
            _reg = std::forward<Fn>(fn);
        }




    protected:
        virtual void resume() noexcept override {
            awaiter::resume_handle().resume();
        }
        virtual std::coroutine_handle<> resume_handle() noexcept override {
            return _h;
        }
        std::weak_ptr<state> _wk_state;
        std::coroutine_handle<> _h;
        function<void(signal<T>::receiver), 4*sizeof(void *)> _reg;
    };

    ///get signal awaiter
    /** You can pass the result to a coroutine which can co_await in it
     *
     * @return awaiter, can be co_awaited if you want to receive a signal
     *
     * @note awaiter holds weak reference. You need to hold receiver or
     * signal instance to keep awaiter connected
     */
    awaiter get_awaiter() const {
        return awaiter(_state);
    }

    ///get signal receiver
    /**
     * @return receiver, it acts as function, so can be stored in std::function.
     *
     * @note receiver holds strong reference. The signal state is held if there
     * is at least one receiver.
     */
    receiver get_receiver() const {
        return receiver(_state);
    }

    ///connect a function to the signal
    /**this allows to forward signal or broadcast signal to multiple functions
     *
     * @note the function is not notified that signal has been destroyed.
     * You can only detect, that function has been dropped
     *
     * */

    template<typename Fn>
    CXX20_REQUIRES(
            ((!std::same_as<T,void>) && std::constructible_from<bool, decltype(std::declval<Fn>()(std::declval<reference>()))>)
            ||
            ((std::same_as<T,void>) && std::constructible_from<bool, decltype(std::declval<Fn>()())>)
    )
    void connect(Fn &&fn) {

        class fnawt: public abstract_awaiter {
        public:
            fnawt(Fn &&fn, awaiter &&awt):_fn(std::forward<Fn>(fn)), _awt(std::move(awt)) {}
            virtual void resume() noexcept override {
                try {
                    if constexpr(std::is_void_v<T>) {
                        _awt.await_resume();
                        if (_fn()) {
                            if (!_awt.subscribe_awaiter(this)) {
                                resume();
                            }
                        } else {
                            delete this;
                        }

                    } else {
                        if (_fn(_awt.await_resume())) {
                            if (!_awt.subscribe_awaiter(this)) {
                                resume();
                            }
                        } else {
                            delete this;
                        }
                    }
                } catch (...) {
                    delete this;
                }
            }
            void initial_reg() {
                if (!_awt.subscribe_awaiter(this)) {
                    resume();
                }
            }
        protected:
            Fn _fn;
            awaiter _awt;
        };


        auto *x = new fnawt(std::forward<Fn>(fn), get_awaiter());
        x->initial_reg();
    }


    signal():_state(std::make_shared<state>()) {}


protected:
    std::shared_ptr<state> _state;
    signal(std::shared_ptr<state> x):_state(std::move(x)) {}


};

}

#endif /* SRC_COCLASSES_AWAITABLE_SLOT_H_ */

