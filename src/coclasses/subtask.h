#pragma once
#ifndef SRC_COCLASSES_SUBTASK_H_
#define SRC_COCLASSES_SUBTASK_H_
#include "exceptions.h"
#include "awaiter.h"
#include "debug.h"

#include "co_alloc.h"
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
template<typename T = void>
class subtask;

template<typename T>
class [[nodiscard]] subtask {  
protected:
    enum State {unused,running,result,exception};
    
public:
    
    ///coroutine's promise type
    struct promise_type: coro_allocator { // @suppress("Miss copy constructor or assignment operator")
        ///contains pointer to future - to refer place where to store result
        subtask *_future = nullptr;
        ///contains pointer to awaiter - which will be resumed at the end
        abstract_awaiter<> *_awaiter = nullptr;
        ///final suspender - resumes awaiter at the end of execution 
        struct final_suspender: std::suspend_always { // @suppress("Miss copy constructor or assignment operator")
            final_suspender(promise_type *owner):_owner(owner) {}            
            promise_type *_owner;
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> ) {
                return _owner->_awaiter->resume_handle();
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
        template<typename X, typename = std::enable_if<std::is_convertible_v<X,T> > >
        void return_value(X &&val) {
            new(&_future->_value) T(std::forward<X>(val));;
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
        T &await_resume() {
            switch (this->_owner._state) {
                default:
                case State::unused:
                case State::running: throw value_not_ready_exception();
                case State::result: return this->_owner._value;
                case State::exception: std::rethrow_exception(this->_owner._exception);
            }
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
                case State::result: new(&_value) T(std::move(other._value)) ;break;
                case State::exception: new(&_exception) std::exception_ptr(std::move(other._exception));break;
                case State::running: assert(!"Running subtask can't be moved");break;
                default:break;
            }
    }
    
    ///destructor
    ~subtask() {
        switch (_state) {
            case State::result: _value.~T();break;
            case State::exception: _exception.~exception_ptr();break;
            case State::running: assert(!"Attempt to destroy running subtask");break;
            default: break;
        }
        if (_h) {
 #ifdef COCLS_DEFINE_SET_CORO_NAME
         debug_reporter::current_instance->coro_destroyed(_h);
 #endif
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

    ///Run subtask and wait for result
    T &join() {

        syncing s(*this);
        if (!s.await_ready()) {
            s.await_suspend({}).resume();
            s.sync();
        }
        return s.await_resume();
    }
    
    ///Run subtask and wait for result
    T &value() {return join();}

    
    bool done() const {return _state != State::unused && _state != State::running;}
    
protected:

    std::coroutine_handle<promise_type> _h;
    union {
        T _value;
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


template<>
class [[nodiscard]] subtask<void> {  
public:
    
    enum State {
        unused,
        running,
        result,
        exception
    };
    
    struct promise_type: coro_allocator { // @suppress("Miss copy constructor or assignment operator")
        subtask *_future = nullptr;
        abstract_awaiter<> *_awaiter = nullptr;
        struct final_suspender: std::suspend_always { // @suppress("Miss copy constructor or assignment operator")
            final_suspender(promise_type *owner):_owner(owner) {}            
            promise_type *_owner;
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> ) {
                return _owner->_awaiter->resume_handle();
            }
        };
        std::suspend_always initial_suspend() noexcept {return {};}
        final_suspender final_suspend() noexcept {return final_suspender(this);}
        
        subtask<void> get_return_object() {
            return subtask(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        
        void unhandled_exception() {
            new(&_future->_exception) std::exception_ptr(std::current_exception());
            _future->_state = State::exception;
        }        
        void return_void() {
            _future->_state = State::result;
        }
        
    };
 
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
         void await_resume() {
             switch (this->_owner._state) {
                 default:
                 case State::unused:
                 case State::running: throw value_not_ready_exception();
                 case State::result: break;
                 case State::exception: std::rethrow_exception(this->_owner._exception);
             }
         }

         
     };
    
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

    
    subtask(std::coroutine_handle<promise_type> h):_h(h),_state(State::unused) {}
    subtask(const subtask &) = delete;
    subtask &operator=(const subtask &other) = delete;
    subtask(subtask &&other)
        :_h(other._h)
        ,_exception(std::move(other._exception))
        ,_state(other._state) {
            other._h = {};
    }
    
    ~subtask() {
       if (_h) {
#ifdef COCLS_DEFINE_SET_CORO_NAME
        debug_reporter::current_instance->coro_destroyed(_h);
#endif
      _h.destroy();
       }
    }
    
    static subtask<void> set_result() {return subtask<void>(State::result, {});}
    static subtask<void> set_exception() {return subtask<void>(State::exception, std::current_exception());}
    static subtask<void> set_exception(std::exception_ptr e) {return subtask<void>(State::exception, std::move(e));}

    void join() {

        syncing s(*this);
        if (!s.await_ready()) {
            s.await_suspend({}).resume();
            s.sync();
        }
        s.await_resume();
    }
    
    void value() {join();}
    
    bool done() const {return _state != State::unused && _state != State::running;}

    
protected:
    std::coroutine_handle<promise_type> _h;
    std::exception_ptr _exception;
    State _state;
    
    subtask(State state, std::exception_ptr exception) 
        :_exception(std::move(exception))
        ,_state(state) {}
};

}



#endif /* SRC_COCLASSES_SUBTASK_H_ */
