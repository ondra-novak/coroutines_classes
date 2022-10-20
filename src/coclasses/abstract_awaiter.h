/** @file abstract_awaiter.h */

#ifndef SRC_COCLASSES_ABSTRACT_AWAITER_H_
#define SRC_COCLASSES_ABSTRACT_AWAITER_H_

#include "resumption_policy.h"
#include "queued_resumption_policy.h"

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
 * auto get_result(coroutines); - return result
 * 
 */

class abstract_awaiter_base {
public:

    ///called to resume coroutine
    virtual void resume() noexcept = 0;

    ///called to retrieve coroutine handle for symmetric transfer
    virtual std::coroutine_handle<> resume_handle() {
        resume();
        return std::noop_coroutine();
    }
    virtual ~abstract_awaiter_base() = default;
};

template<bool chain = false>
class abstract_awaiter: public abstract_awaiter_base {
};

template<>
class abstract_awaiter<true>: public abstract_awaiter_base {
public:
    abstract_awaiter() = default;
    abstract_awaiter(const abstract_awaiter &)=default;
    abstract_awaiter &operator=(const abstract_awaiter &)=delete;
    
    void subscribe(std::atomic<abstract_awaiter *> &chain) {
        while (!chain.compare_exchange_weak(_next, this, std::memory_order_relaxed));
    }
    ///releases chain atomicaly
    /** 
     * @param chain holds chain
     * @param skip awaiter to be skipped
     * @return count of released awaiters (including skipped)
     */
    static std::size_t resume_chain(std::atomic<abstract_awaiter *> &chain, abstract_awaiter *skip) {
        return resume_chain_lk(chain.exchange(nullptr), skip);
    }
    static std::size_t resume_chain_lk(abstract_awaiter *chain, abstract_awaiter *skip) {
        std::size_t n = 0;
        while (chain) {
            auto y = chain;
            chain = chain->_next;
            if (y != skip) y->resume();
            n++;
        }
        return n;
    }

    abstract_awaiter *_next = nullptr;
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

template<typename promise_type, typename policy = void, bool chain = false>
class co_awaiter;

template<typename promise_type, bool chain = false>
class co_awaiter_base: public abstract_owned_awaiter<promise_type, chain> {
public:
    using abstract_owned_awaiter<promise_type, chain>::abstract_owned_awaiter;

    ///co_await related function
    bool await_ready() {
        return this->_owner.is_ready();
    }
    ///co_await related function
    bool await_suspend(std::coroutine_handle<> h) {
        _h = h; 
        return this->_owner.subscribe_awaiter(this);
    }
    ///co_await related function
    decltype(auto) await_resume(){
        return this->_owner.get_result();
    }
    
    ///Wait synchronously
    /**
     * Blocks execution until awaiter is signaled
     * @return result of awaited operation
     */
    decltype(auto) wait();
    
    
    ///Subscribe custom awaiter
    /**
     * Allows to declare custom awaiter, which is resumed, when awaited result is ready. 
     * @param awt reference to awaiter. You need to keep reference valid until it is called
     * @retval true registration done
     * @retval false awaiting expression is already resolved, so no registration done, you can
     * call await_resume()
     */
    bool subscribe_awaiter(abstract_awaiter<chain> *awt) {
        return this->_owner.subscribe_awaiter(awt);
    }
    
    
    
    
#ifdef __CDT_PARSER__
    //this helps to Eclipse CDT parser to recognize co_await conversion  
    using ReturnValue = decltype(std::declval<co_awaiter_base<promise_type,chain> >().await_resume());

    operator ReturnValue();
#endif
protected:
    std::coroutine_handle<> _h;

    virtual std::coroutine_handle<> resume_handle() noexcept  override {
        return _h;
    }

};

///Awaiter which can be awaited by co_await
/**
 * In most of cases, this object is returned by co_await on various primitives
 * @tparam promise_type type of promise
 * @tparam policy resume policy
 * @tparam chain set true if the awaiter can be chained, otherwise false
 */
template<typename promise_type, typename policy , bool chain>
class co_awaiter: public co_awaiter_base<promise_type, chain> {
public:

    co_awaiter(promise_type &owner):co_awaiter_base<promise_type, chain>(owner) {}
    co_awaiter(policy p, promise_type &owner)
            :co_awaiter_base<promise_type, chain>(owner)
            ,_p(std::forward<policy>(p)) {}
    
    
    virtual void resume() noexcept override  {
        _p.resume(this->_h);
    }
    ///Allows to change resumption policy.
    /** This member is called by a task, when await_transform, to supply
     * own policy
     *
     * @param p resumption policy
     * @return
     */
    template<typename _Policy>
    co_awaiter<promise_type, _Policy, chain> set_resumption_policy(_Policy p); 
protected:
    [[no_unique_address]] policy _p;
};

template<typename promise_type, bool chain>
class co_awaiter<promise_type, void, chain>: public co_awaiter<promise_type, resumption_policy::unspecified<void>, chain> {
public:
    using co_awaiter<promise_type, resumption_policy::unspecified<void>, chain>::co_awaiter;
};;

template<typename promise_type, bool chain = false>
class blocking_awaiter: public abstract_owned_awaiter<promise_type, chain> {
public:
    using abstract_owned_awaiter<promise_type, chain>::abstract_owned_awaiter;
    
    decltype(auto) wait() {
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
    
    virtual void resume() noexcept  override {
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
    
    decltype(auto) operator()() {
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
    virtual void resume() override {
        _fn(callback_result<promise_type>(this->_owner));
    }
};

template<typename promise_type, bool chain>
inline decltype(auto) co_awaiter_base<promise_type, chain>::wait() {
    blocking_awaiter<promise_type, chain> x(this->_owner);
    return x.wait();
}


template<typename promise_type, typename Policy, bool chain>
template<typename _Policy>
inline co_awaiter<promise_type, _Policy, chain> co_awaiter<
            promise_type, Policy, chain>::set_resumption_policy(_Policy p) {
    return co_awaiter<promise_type,_Policy, chain>(std::forward<_Policy>(p), this->_owner); 
}

}
#endif /* SRC_COCLASSES_ABSTRACT_AWAITER_H_ */
