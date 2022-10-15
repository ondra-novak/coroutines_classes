/*
 * abstract_awaiter.h
 *
 *  Created on: 7. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_ABSTRACT_AWAITER_H_
#define SRC_COCLASSES_ABSTRACT_AWAITER_H_


#include <algorithm>
#include <condition_variable>
#include <coroutine> 
#include <mutex>

namespace cocls {

///Abstract awaiter
/**
 * allows to implement both co_awaiter and blocking_awaiter
 * @tparam promise_type type of promise (of task for example)
 * @tparam chain set this true (default false) to allow chains (includes next ptr to awaiter)
 * 
 * The promise must implement
 * 
 * bool is_ready(); - return true, if result is ready
 * bool subscribe_awaiter(abstract_awaiter *); - return true, if set, false if resolved
 * auto get_result(); - return result
 * 
 */

template<bool chain = false>
class abstract_awaiter {
public:
    virtual void resume() = 0;
    virtual ~abstract_awaiter() = default;
};

template<>
class abstract_awaiter<true> {
public:
    abstract_awaiter()=default;
    abstract_awaiter(const abstract_awaiter &)=default;
    abstract_awaiter &operator=(const abstract_awaiter &)=delete;
    virtual ~abstract_awaiter() = default;
    
    virtual void resume() = 0;

    void subscribe(std::atomic<abstract_awaiter *> &chain) {
        while (!chain.compare_exchange_weak(_next, this, std::memory_order_relaxed));
    }
    static void resume_chain(std::atomic<abstract_awaiter *> &chain, abstract_awaiter *skip) {
        resume_chain_lk(chain.exchange(nullptr), skip);
    }
    static void resume_chain_lk(abstract_awaiter *chain, abstract_awaiter *skip) {
        while (chain) {
            auto y = chain;
            chain = chain->_next;
            if (y != skip) y->resume();
        }
    }

    
    abstract_awaiter<true> *_next = nullptr;
protected:
};

template<typename promise_type, bool chain = false>
class abstract_owned_awaiter: public abstract_awaiter<chain> {
public:
    abstract_owned_awaiter(promise_type &owner):_owner(owner) {}
    abstract_owned_awaiter(const abstract_owned_awaiter  &) = default;
    abstract_owned_awaiter &operator=(const abstract_owned_awaiter &) = delete;


    
protected:
    promise_type &_owner;
};

template<typename promise_type>
class abstract_owned_awaiter<promise_type, true>: public abstract_awaiter<true> {
public:

    abstract_owned_awaiter(promise_type &owner):_owner(owner) {}
    abstract_owned_awaiter(const abstract_owned_awaiter &)=default;
    abstract_owned_awaiter &operator=(const abstract_owned_awaiter &)=delete;

protected:
    promise_type &_owner;
    
};


///Awaiter which can be awaited by co_await
/**
 * In most of cases, this object is returned by co_await on various primitives
 * @tparam promise_type type of promise
 * @tparam chain set true if the awaiter can be chained, otherwise false
 */
template<typename promise_type, bool chain = false>
class co_awaiter: public abstract_owned_awaiter<promise_type, chain> {
public:
    
    
    co_awaiter(promise_type &owner):abstract_owned_awaiter<promise_type, chain>(owner) {}
    
    ///co_await related function
    bool await_ready() {
        return this->_owner.is_ready();
    }
    ///co_await related function
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        _h = h; 
        if (this->_owner.subscribe_awaiter(this)) {
            return resume_lock::await_suspend();
        } else {
            return h; 
        }
    }
    ///co_await related function
    auto await_resume() {
        return this->_owner.get_result();
    }
    
    ///Wait synchronously
    /**
     * Blocks execution until awaiter is signaled
     * @return result of awaited operation
     */
    auto wait();
    
    
    ///Subscribe custom awaiter
    /**
     * Allows to declare custom awaiter, which is resumed, when awaited result is ready. 
     * @param awt reference to awaiter. You need to keep reference valid until it is called
     * @retval true registration done
     * @retval false awaiting expression is already resolved, so no registration done, you can
     * call await_resume()
     */
    bool subscribe_awaiter(abstract_awaiter<> *awt) {
        return this->_owner.subscribe_awaiter(awt);
    }
    
    
protected:
    std::coroutine_handle<> _h;
    virtual void resume() {
        resume_lock::resume(_h);
    }
};

template<typename promise_type, bool chain = false>
class blocking_awaiter: public abstract_owned_awaiter<promise_type, chain> {
public:
    blocking_awaiter(promise_type &owner):abstract_owned_awaiter<promise_type, chain>(owner) {}
    
    auto wait() {
        _signal = this->_owner.is_ready();
        if (!_signal && this->_owner.subscribe_awaiter(this)) {
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
class callback_result {
public:
    callback_result(promise_type &owner):_owner(owner) {}
    callback_result(const callback_result &owner) = default;
    callback_result &operator=(const callback_result &owner) = delete;
    
    auto operator()() {
        return _owner.get_result();
    }
protected:
    promise_type &_owner;   
};


template<typename Fn, typename promise_type, bool chain = false>
class callback_awaiter: public abstract_owned_awaiter<promise_type, chain> {
public:
    callback_awaiter(Fn &&callback, promise_type &owner):abstract_owned_awaiter<promise_type, chain>(owner) {}
    
    void charge() {
        if (!this->_owner.is_ready() 
                && this->_owner.subscribe_awaiter(this)) {
            return;
        }
        _fn(callback_result<promise_type>(this->_owner));
    }
    
protected:
    virtual void resume() {
        _fn(callback_result<promise_type>(this->_owner));
    }
};

template<typename promise_type, bool chain>
inline auto co_awaiter<promise_type, chain>::wait() {
    blocking_awaiter<promise_type, chain> x(this->_owner);
    return x.wait();
}

}


#endif /* SRC_COCLASSES_ABSTRACT_AWAITER_H_ */
