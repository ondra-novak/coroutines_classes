#pragma once
#ifndef SRC_COCLASSES_SUBTASK_H_
#define SRC_COCLASSES_SUBTASK_H_
#include "exceptions.h"
#include "awaiter.h"
#include "debug.h"

#include "poolalloc.h"
#include <cassert>
#include <coroutine>
#include <exception>
#include <type_traits>
#include <utility>





namespace cocls {
/**
 * Subtask - it is small coroutine, which can be used as subtask of other task.
 * It has following traits
 *
 * - The coroutine can be awaited by only one awaiter (task can be awaited by multiple
 * awaiters).
 * - The coroutine is not started immediately. IT MUST BE AWAITED to be started.
 * (similar as lazy<>)
 * - The coroutine is always started using symmetric transfer from awaiting coroutine
 *  to the subtask coroutine
 * - The coroutine doesn't define resumption policy. This is equivalent to specify
 * unspecified policy. However resumption from other coroutines is probably done
 * by the symmetric transfer.
 * - There is no atomic, no locks. This is the reason, why such coroutine type exists
 * - Hopefully allocation elision (if it is supported)
 * - The coroutine defines join() and value(), both causes that coroutine is
 *   started synchronously (like a function)
 * - The coroutine supports coro_allocator
 * - The coroutine defines function set_result() and set_exception to construct
 *   the coroutine's future without actually run the coroutine
 * - The coroutine's future is movable when it is not running
 *
 *
 * @tparam T
 */

template<typename T>
class [[nodiscard]] subtask {
protected:
    enum State {unused,running,result,exception};

public:

    static constexpr bool is_void = std::is_void_v<T>;
    using value_type = T;
    using reference = std::add_lvalue_reference_t<value_type>;
    using const_reference = std::add_const_t<std::add_lvalue_reference_t<value_type> >;
    using value_type_storage = std::conditional_t<is_void,int,value_type>;

    ///coroutine's promise type
    struct promise_type: coro_promise_base, coro_unified_return<value_type, typename subtask<T>::promise_type> { // @suppress("Miss copy constructor or assignment operator")
        ///contains pointer to future - to refer place where to store result
        subtask *_future = nullptr;
        ///contains pointer to awaiter - which will be resumed at the end
        abstract_awaiter<> *_awaiter = nullptr;
        ///final suspender - resumes awaiter at the end of execution
        struct final_suspender: std::suspend_always { // @suppress("Miss copy constructor or assignment operator")
            final_suspender(promise_type *owner):_owner(owner) {}
            promise_type *_owner;
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
                auto awt =_owner->_awaiter;
                _owner->_future->_h = {};
                h.destroy();
                return awt->resume_handle();
            }
        };
        ///Specifies, that coroutine is started suspended
        /** the coroutine is resumed by co_await */
        std::suspend_always initial_suspend() noexcept {return {};}
        ///Specifies how coroutine finishes
        final_suspender final_suspend() noexcept {return final_suspender(this);}
        ///retrieves the future of the coroutine
        subtask<T> get_return_object() {
            return subtask(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        ///called when exception happens in the body of the coroutine
        void unhandled_exception() {
            new(&_future->_exception) std::exception_ptr(std::current_exception());
            _future->_state = State::exception;
        }
        ///called to store result
        template<typename ... Args>
        CXX20_REQUIRES(std::constructible_from<value_type_storage, Args...>)
        void resolve(Args && ... args) {
            new(&_future->_value) value_type_storage(std::forward<Args>(args)...);;
            _future->_state = State::result;
        }

    };

    ///awaiter is helper object which handles co_await interface
    /**
     * We use "satellite" awaiter to easily change resumption policy
     */
    class [[nodiscard]] awaiter: public co_awaiter_policy_base<subtask> {
    public:
        using co_awaiter_policy_base<subtask>::co_awaiter_policy_base;

        bool await_ready() const {
            return this->_owner._state != State::unused;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            this->_owner._state = State::running;
            this->_h = h;
            promise_type &p = this->_owner._h.promise();
            p._awaiter = this;
            p._future = &this->_owner;
            return this->_owner._h;
        }
        reference await_resume() {
            switch (this->_owner._state) {
                default:
                case State::unused:
                case State::running: throw value_not_ready_exception();
                case State::result: if constexpr(is_void) return;
                                    else return this->_owner._value;
                case State::exception: std::rethrow_exception(this->_owner._exception);
            }
        }

        ///compatibility with co_awaiter
        bool subscribe_awaiter(abstract_awaiter<false> *awt) {
            promise_type &p = this->_owner._h.promise();
            p._awaiter = awt;
            p._future = &this->_owner;
            this->_owner._h.resume();
            return true;

        }
    };

    ///Retrieves the awaiter
    awaiter operator co_await() {
        return *this;
    }

    ///Helper class supports join()
    class syncing: public awaiter {
    public:
        using awaiter::awaiter;
        void sync() {
            flag.wait(false);
        }
        virtual void resume() noexcept override {
            flag.store(true, std::memory_order_release);
            flag.notify_all();
        }
        virtual std::coroutine_handle<> resume_handle() noexcept override {
            syncing::resume();
            return std::noop_coroutine();
        }
    protected:
        std::atomic<bool> flag = {false};
    };

    ///construct subtask future by the coroutine itself
    /**
     * @param h handle of coroutine
     */
    subtask(std::coroutine_handle<promise_type> h):_h(h),_state(State::unused) {}
    ///can't be copied
    subtask(const subtask &) = delete;
    ///can't be assigned
    subtask &operator=(const subtask &other) = delete;
    ///can move
    subtask(subtask &&other)
        :_h(other._h)
        ,_state(other._state) {
            other._h = {};
            switch (_state) {
                case State::result: new(&_value) value_type_storage(std::move(other._value)) ;break;
                case State::exception: new(&_exception) std::exception_ptr(std::move(other._exception));break;
                case State::running: assert(!"Running subtask can't be moved");break;
                default:break;
            }
    }
    subtask &operator=(subtask &&other) {
        if (this != &other) {
            this->~subtask();
            new(this) subtask(std::move(other));
        }
        return *this;
    }

    ///destructor
    ~subtask() {
        switch (_state) {
            case State::result: _value.~value_type_storage();break;
            case State::exception: _exception.~exception_ptr();break;
            case State::running: assert(!"Attempt to destroy running subtask");break;
            default: break;
        }
        if (_h) {
            _h.destroy();
        }
    }

    ///Construct subtask's future by setting the result
    /**
     * This is useful when result is already known without need to
     * execute the coroutine, which can be less effective.
     * @param args arguments need to construct result
     * @return
     */
    template<typename ... Args>
    static subtask set_result(Args && ... args) {
        return subtask(std::forward<Args>(args)...);
    }
    ///Construct subtask's future by setting an exception
    static subtask set_exception(std::exception e) {
        return subtask(std::move(e));
    }
    ///Construct subtask's future by setting the current exception
    static subtask set_exception() {
        return subtask(std::current_exception());
    }
    static subtask set_empty() {
        return subtask((std::coroutine_handle<promise_type>()));
    }

    ///Run subtask and wait for result
    reference join() {

        syncing s(*this);
        if (!s.await_ready()) {
            s.await_suspend({}).resume();
            s.sync();
        }
        return s.await_resume();
    }

    ///Run subtask and wait for result
    reference value() {return join();}


    bool done() const {return _state != State::unused && _state != State::running;}

protected:

    std::coroutine_handle<promise_type> _h;
    union {
        value_type_storage _value;
        std::exception_ptr _exception;
    };
    State _state;

    template<typename ... Args>
    explicit subtask(Args &&...args)
        :_value(std::forward<Args>(args)...)
        ,_state(State::result) {}
    explicit subtask(std::exception_ptr &&x)
        :_exception(std::move(x))
        ,_state(State::exception) {}


    friend class co_awaiter<subtask>;

};


}



#endif /* SRC_COCLASSES_SUBTASK_H_ */
