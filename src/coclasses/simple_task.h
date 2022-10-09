#pragma once
#ifndef SRC_COCLASSES_SIMPLE_TASK_H_
#define SRC_COCLASSES_SIMPLE_TASK_H_
#include "value_or_exception.h"

#include "resume_lock.h"

#include "abstract_awaiter.h"
#include <condition_variable>
#include <coroutine>
#include <memory>
#include <mutex>



namespace cocls {

template<typename T>
class simple_task_promise;


///Simple task is similar to task, but have much simpler promise and allows only one co_await at time
template<typename T>
class simple_task {
public:

    using promise_type = simple_task_promise<T>;

    
    simple_task();
    simple_task(promise_type *ptr):_ptr(ptr) {}
    
    simple_task(simple_task &&other) = default;
    simple_task &operator=(simple_task &&other) = default;
    
    bool await_ready() {
        return _ptr->is_ready();
    }
    
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        return _ptr->set_awaiter(h);
    }
    
    auto join() {
        blocking_awaiter<promise_type> aw(*_ptr);
        return aw.wait();
    }
    
    co_awaiter<promise_type> operator co_await() {
        return co_awaiter<promise_type>(*_ptr);
    }
    
    
protected:
    
    struct Deleter {
        void operator()(promise_type *ptr) {
            ptr->result_destroyed();
        }
    };
    
    std::unique_ptr<promise_type, Deleter> _ptr;
    
};


template<typename T>
class simple_task_promise_base {
public:
    
    simple_task_promise_base() {}
    simple_task_promise_base(const simple_task_promise_base &) = delete;
    simple_task_promise_base &operator=(const simple_task_promise_base &) = delete;
    
    enum class State {
        //task running
        running,
        //task running, but noone needs result (result destroyed)
        zombie,
        //task running, and it is awaited
        awaited,
        //task finished, waiting to pick result
        finished,
        
    };
    
    std::atomic<State> _state;
    value_or_exception<T> _value;
    abstract_awaiter<simple_task_promise<T> > *_awaiter = nullptr;
    
    void result_destroyed() {
        State s = _state.exchange(State::zombie);
        assert(s == State::running || s == State::finished);
        if (s == State::finished) {
            std::coroutine_handle<simple_task_promise_base>::from_promise(*this).destroy();
        }
    }
    
    bool set_awaiter(abstract_awaiter<simple_task_promise<T> > *aw) {
        _awaiter = aw;
        State s = State::running;
        if (!_state.compare_exchange_strong(s, State::awaited)) {
            assert(s != State::awaited); //can't await twice on simple_task
            return false;
        }  else {
            return true;
        }
    }
    
    bool is_ready() const {
        return _state == State::finished;
    }
    
    auto get_result() {
        return _value.get_value();
    }
    
    class finisher {
    public:
        finisher (simple_task_promise_base &p):_p(p) {}
        finisher (const finisher &) = default;
        finisher &operator=(const finisher &) = delete;
        
        bool await_ready() const noexcept {
            return _p._state == State::zombie;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept {            
            return resume_lock::await_suspend();
        }
        static void await_resume() noexcept {}
        
        simple_task_promise_base &_p;
    };
    
    static std::suspend_never initial_suspend() noexcept {return {};}
    finisher final_suspend() noexcept {return *this;}
    
    void finish() {
        State s = State::running;
        if (!_state.compare_exchange_strong(s, State::finished)) {
            if (s == State::awaited) {
                _state = State::finished;
                _awaiter->resume();
            }            
        }
    }
    
    
    
    void unhandled_exception() {
        _value.unhandled_exception();
        finish();
    }
    
};


template<typename T>
class simple_task_promise: public simple_task_promise_base<T> {
public:
    template<typename X>
    auto return_value(X &&val) -> decltype(std::declval<value_or_exception<T> >().set_value(std::declval<X>())) {
        this->_value.set_value(std::forward<X>(val));
        this->finish();
    }
    simple_task<T> get_return_object() {
        return simple_task<T>(this); 
    }
};

template<>
class simple_task_promise<void>: public simple_task_promise_base<void> {
    void return_void(){
        this->_value.set_value();
        this->finish();        
    }
    simple_task<void> get_return_object() {
        return simple_task<void>(this); 
    }
};

}




#endif /* SRC_COCLASSES_SIMPLE_TASK_H_ */
