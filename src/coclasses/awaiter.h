/** @file awaiter.h */

#ifndef SRC_COCLASSES_AWAITER_H_
#define SRC_COCLASSES_AWAITER_H_

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
public:
    ///subscribe this awaiter but at the same time, check, whether it is marked ready
    /**
     * @param chain register to chain
     * @retval true registered
     * @retval false failed to register, object is already ready
     */
    bool subscibre_check_ready(std::atomic<abstract_awaiter *> &chain_);
    ///Mark object ready and resume chain 
    /**
     * @param chain chain to resume
     * @param skip skip specified awaiter
     */
    static void mark_ready_resume(std::atomic<abstract_awaiter *> &chain_);
    ///Check whether ready
    /**
     * @param chain chain to check
     * @return
     */
    static bool is_ready(const std::atomic<abstract_awaiter *> &chain_);
    ///mark object processed (must be ready);
    /**
     * @param chain chain to check
     * @retval true marked
     * @retval false object is not ready
     */
    static bool mark_processed(std::atomic<abstract_awaiter *> &chain_);

    static bool is_processed(const std::atomic<abstract_awaiter *> &chain_);
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
    ///subscribe this awaiter but at the same time, check, whether it is marked ready
    /**
     * @param chain register to chain
     * @retval true registered
     * @retval false failed to register, object is already ready
     */
    bool subscibre_check_ready(std::atomic<abstract_awaiter *> &chain);
    ///Mark object ready and resume chain 
    /**
     * @param chain chain to resume
     * @param skip skip specified awaiter
     */
    static void mark_ready_resume(std::atomic<abstract_awaiter *> &chain);
    ///Check whether ready
    /**
     * @param chain chain to check
     * @return
     */
    static bool is_ready(const std::atomic<abstract_awaiter *> &chain);
    
    ///mark object processed (must be ready);
    /**
     * @param chain chain to check
     * @retval true marked
     * @retval false object is not ready
     */
    static bool mark_processed(std::atomic<abstract_awaiter *> &chain);
    
    static bool is_processed(const std::atomic<abstract_awaiter *> &chain);

    abstract_awaiter *_next = nullptr;
protected:
};

template<bool chain>
class empty_awaiter: public abstract_awaiter<chain> {
public:
    ///marks awaiter pointer as "ready" which prevents to register awaiter and reporting that object is ready
    static empty_awaiter<chain> ready;
    ///mark awaiter pointer as "processed" - same as ready, but value has been read
    static empty_awaiter<chain> processed;
    virtual void resume() noexcept {}
};

///useful to set other than null
template<bool chain>
empty_awaiter<chain> empty_awaiter<chain>::ready;
template<bool chain>
empty_awaiter<chain> empty_awaiter<chain>::processed;

///
template<typename promise_type, bool chain = false>
class abstract_owned_awaiter: public abstract_awaiter<chain> {
public:    
    abstract_owned_awaiter(promise_type &owner):_owner(owner) {}
    abstract_owned_awaiter(const abstract_owned_awaiter  &) = default;
    abstract_owned_awaiter &operator=(const abstract_owned_awaiter &) = delete;


    
protected:
    promise_type &_owner;

};

template<typename parent, typename policy>
class co_awaiter_policy;

///Generic awaiter used in most object to handle co_await
template<typename promise_type, bool chain = false>
class co_awaiter: public abstract_owned_awaiter<promise_type, chain> {
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

    ///Allows to change resumption policy.
    /** This member is called by a task, when await_transform, to supply
     * own policy. This method is static to able work well on derived awaiters
     * 
     * @param _this reference to _this object, but in the most child type
     * @param p resumption policy
     * @return awaiter with policy
     */
    template<typename _This, typename _Policy>
    static co_awaiter_policy<_This, _Policy> set_resumption_policy(_This &&_this, _Policy &&p) {
        return co_awaiter_policy<_This, _Policy>(std::forward<_Policy>(p), std::forward<_This>(_this));
    }
    
#ifdef __CDT_PARSER__
    //this helps to Eclipse CDT parser to recognize co_await conversion  
    using ReturnValue = decltype(std::declval<co_awaiter<promise_type,chain> >().await_resume());

    operator ReturnValue();
#endif
protected:
    std::coroutine_handle<> _h;

    virtual std::coroutine_handle<> resume_handle() noexcept  override {
        return _h;
    }
    virtual void resume() noexcept  override {
        resumption_policy::unspecified<void>::resume(_h);
    }

    template<typename,typename> friend class co_awaiter_policy;
    
};

///Awaiter with policy
template<typename parent, typename policy>
class co_awaiter_policy: public std::remove_reference<parent>::type {
public:    
    using super = typename std::remove_reference<parent>::type;
    co_awaiter_policy(policy &&p, parent &&owner)
            :super(std::forward<parent>(owner))
            ,_p(std::forward<policy>(p)) {}
            

    bool await_ready() {
        return super::await_ready();
    }
    decltype(auto) await_suspend(std::coroutine_handle<> h) {
        return super::await_suspend(h);
    }    
    decltype(auto) await_resume(){
        if (_resume_exception) {
            std::rethrow_exception(_resume_exception);
        }
        return super::await_resume();
    }
    
    virtual void resume() noexcept override  {
        try {
            _p.resume(super::_h);
        } catch (...) {
            _resume_exception = std::current_exception();
            super::_h.resume();
        }
    }
    
    static co_awaiter_policy set_resumption_policy(co_awaiter_policy  &&_this, policy &&p) {
        return _this;
    }

protected:
    [[no_unique_address]] policy _p;
    std::exception_ptr _resume_exception;    
};

///Awaiter which allows to block thread until value is ready
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

template<typename promise_type, bool chain>
inline decltype(auto) co_awaiter<promise_type, chain>::wait() {
    blocking_awaiter<promise_type, chain> x(this->_owner);
    return x.wait();
}

template<bool chain>
inline bool abstract_awaiter<chain>::subscibre_check_ready(std::atomic<abstract_awaiter*> &chain_) {
    abstract_awaiter *n = nullptr;
    //register only if there is non-zero value
    //then result is ready, however if there is other awaiter, it is UB
    return chain_.compare_exchange_strong(n, this, std::memory_order_relaxed);
}

template<bool chain>
inline void abstract_awaiter<chain>::mark_ready_resume(std::atomic<abstract_awaiter*> &chain_) {
    abstract_awaiter *h = chain_.exchange(empty_awaiter<chain>::instance, std::memory_order_seq_cst);
    if (h) h->resume();
}

template<bool chain>
inline bool abstract_awaiter<chain>::is_ready(const std::atomic<abstract_awaiter*> &chain_) {
    auto st = chain_.load(std::memory_order_relaxed); 
    return  st == &empty_awaiter<true>::ready || st == &empty_awaiter<true>::processed;
}

template<bool chain>
inline bool abstract_awaiter<chain>::is_processed(const std::atomic<abstract_awaiter*> &chain_) {
    auto st = chain_.load(std::memory_order_relaxed); 
    return  st == &empty_awaiter<true>::processed;
}

inline bool abstract_awaiter<true>::subscibre_check_ready(std::atomic<abstract_awaiter<true> *>& chain_) {
    //try to put self to the top of chain - assume _next = nullptr
    while (!chain_.compare_exchange_strong(_next, this, std::memory_order_relaxed)) {
        //failed because _chain != _next
        //see what is there (stored to _next)  - if it is ready or processed, we can't register
        if (_next == &empty_awaiter<true>::ready || _next == &empty_awaiter<true>::processed) {
            //reset _next to be able detect it again
            _next = nullptr;
            //return false
            return false;
        }
    }
    //we successfully subscribed
    return true;
}

inline void abstract_awaiter<true>::mark_ready_resume(std::atomic<abstract_awaiter*>& chain)
{
    abstract_awaiter *a = chain.exchange(&empty_awaiter<true>::ready, std::memory_order_seq_cst);
    while (a) {
        auto b = a;
        a = a->_next;
        b->resume();
    }
}

inline bool abstract_awaiter<true>::is_ready(const std::atomic<abstract_awaiter*>& chain)
{
    auto st = chain.load(std::memory_order_relaxed); 
    return  st == &empty_awaiter<true>::ready || st == &empty_awaiter<true>::processed;
}

inline bool abstract_awaiter<true>::is_processed(const std::atomic<abstract_awaiter*>& chain) {
    auto st = chain.load(std::memory_order_relaxed); 
    return  st == &empty_awaiter<true>::processed;    
}


template<bool chain>
inline bool abstract_awaiter<chain>::mark_processed(std::atomic<abstract_awaiter*> &chain_) {
    abstract_awaiter *n = &empty_awaiter<chain>::ready;
    chain_.compare_exchange_strong(n, &empty_awaiter<chain>::processed, std::memory_order_relaxed);
    return n == &empty_awaiter<chain>::ready || n == &empty_awaiter<chain>::processed;

}


inline bool abstract_awaiter<true>::mark_processed(std::atomic<abstract_awaiter*>& chain)
{
    abstract_awaiter *n = &empty_awaiter<true>::ready;
    chain.compare_exchange_strong(n, &empty_awaiter<true>::processed, std::memory_order_relaxed);
    return n == &empty_awaiter<true>::ready || n == &empty_awaiter<true>::processed;
}


}
#endif /* SRC_COCLASSES_AWAITER_H_ */

