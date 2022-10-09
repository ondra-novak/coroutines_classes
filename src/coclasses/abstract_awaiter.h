/*
 * abstract_awaiter.h
 *
 *  Created on: 7. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_ABSTRACT_AWAITER_H_
#define SRC_COCLASSES_ABSTRACT_AWAITER_H_


#include <algorithm>
#include <coroutine> 

namespace cocls {

///Abstract awaiter
/**
 * allows to implement both co_awaiter and blocking_awaiter
 * @tparam promise_type type of promise (of task for example)
 * 
 * The promise must implement
 * 
 * bool is_ready(); - return true, if result is ready
 * bool set_awaiter(abstract_awaiter *); - return true, if set, false if resolved
 * auto get_result(); - return result
 * 
 */

template<typename promise_type>
class abstract_awaiter {
public:
    abstract_awaiter(promise_type &owner):_owner(owner) {}
    abstract_awaiter(const abstract_awaiter  &) = default;
    abstract_awaiter &operator=(const abstract_awaiter &) = delete;

    virtual void resume() = 0;
    virtual ~abstract_awaiter() = default;
    
protected:
    promise_type &_owner;
};

template<typename promise_type>
class co_awaiter: public abstract_awaiter<promise_type> {
public:
    co_awaiter(promise_type &owner):abstract_awaiter<promise_type>(owner) {}
    bool await_ready() {
        return this->_owner.is_ready();
    }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        _h = h; 
        if (this->_owner.set_awaiter(this)) {
            return resume_lock::await_suspend();
        } else {
            return h; 
        }
    }
    auto await_resume() {
        return this->_owner.get_result();
    }
    
    auto wait();
    
protected:
    std::coroutine_handle<> _h;
    virtual void resume() {
        resume_lock::resume(_h);
    }

    
};

template<typename promise_type>
class blocking_awaiter: public abstract_awaiter<promise_type> {
public:
    blocking_awaiter(promise_type &owner):abstract_awaiter<promise_type>(owner) {}
    
    auto wait() {
        _signal = this->_owner.is_ready();
        if (!_signal &&this->_owner.set_awaiter(this)) {
            std::unique_lock _(_mx);
            _cond.wait(_,[this]{return _signal;});
        }
        return this->_owner.get_result();
    }
        
    
protected:
    std::mutex _mx;
    std::condition_variable _cond;
    bool _signal = false;
    
    virtual void resume() {
        std::unique_lock _(_mx);
        _signal = true;
        _cond.notify_all();
    }        
};

template<typename promise_type>
inline auto co_awaiter<promise_type>::wait() {
    blocking_awaiter<promise_type> x(this->_owner);
    return x.wait();
}

}


#endif /* SRC_COCLASSES_ABSTRACT_AWAITER_H_ */
