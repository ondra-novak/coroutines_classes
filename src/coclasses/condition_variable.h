#pragma once
#ifndef SRC_COCLASSES_CONDITION_VARIABLE_H_
#define SRC_COCLASSES_CONDITION_VARIABLE_H_

#include "common.h"
#include "exceptions.h"
#include "resume_lock.h"

#include <atomic>
#include <coroutine>
#include <mutex>
#include <queue>



namespace cocls {


///Conditiona variable for coroutines
/**
 * works similar to standard coroutine. You only need co_await on variable to wait
 * on signal. You can specify predicate on custom lock and predicate
 * 
 */
class condition_variable {
public:

    
    class abstract_awaiter {
    public:
        virtual bool check_predicate() noexcept = 0;
        virtual void resume() = 0;
        virtual void resume_direct() = 0;
        virtual ~abstract_awaiter() = default;
    };

    struct empty_predicate{};

    
    template<typename Pred> static bool check_before(Pred &fn) {return fn();} 
    template<typename Pred> static bool check_after(Pred &fn) {return fn();}
    static bool check_before(empty_predicate &fn) {return false;} 
    static bool check_after(empty_predicate &fn) {return true;}
        
    template<typename L, typename Pred>
    class awaiter: public abstract_awaiter {
    public:
        awaiter(condition_variable &owner, L &lk, Pred &&pred)
            :_owner(owner), _lock(lk), _pred(pred) {}
        
        awaiter(const awaiter &) = delete;
        awaiter(awaiter &&o):_owner(o._owner),_lock(std::move(o._lock)), _pred(std::forward<Pred>(o._pred)) {}
        awaiter &operator=(const awaiter &) = delete;
        
        bool await_ready() {
            return check_before(_pred);
        }
        std::coroutine_handle<> await_suspend(handle_t h) {            
            _h = h;
            _owner._awaiters.push(this);                
            _lock.unlock();
            return resume_lock::await_suspend(h, true);
        }
        
        void await_resume() {            
            _lock.lock();
            if (_owner._exit) throw await_canceled_exception();
        }
        
        virtual bool check_predicate()  noexcept override{
            std::lock_guard _(_lock);
            return check_after(_pred);
        }
        virtual void resume() override {
            resume_lock::resume(_h);
        }
        virtual void resume_direct() override {
            _h.resume();
        }

    protected:
        condition_variable &_owner;
        handle_t _h;
        L &_lock;
        Pred _pred;
    };
    struct empty_lock{
        static void lock() {};
        static void unlock() {};
    };
        
   
    
    ///co_await on condition variable
    awaiter<empty_lock, empty_predicate> operator co_await() {
        return operator()(); 
    }
    ///co_await on condition variable
    awaiter<empty_lock, empty_predicate> operator()() {
        static empty_lock el;
        return awaiter<empty_lock, empty_predicate>(*this, el,empty_predicate());
    }

    ///wait, you can specify custom mutex
    /**
     * @param mx custom mutex - must implement Lockable. 
     * The mutex must be locked before call and remain locked after call
     * Mutex is unlocked during waiting
     * 
     * @return awaiter 
     */
    template<typename L>
    awaiter<L, empty_predicate> operator()(L &mx) {
        return awaiter<L, empty_predicate>(*this,mx,empty_predicate());
    }
    ///wait you can specify custom mutex and predicate
    /**
     * @param mx custom mutex, must implement Lockable. The mutex must be 
     * locked before co_await and remains locked after resumption. The mutex
     * is unlocked during suspension.
     * @param pred predicate. Predicate is called during co_await and if it
     * is resolved as true, waiting is skipped. The predicate is also called after
     * resume and if the predicate is not fulfilled, the coroutine is kept suspended
     * 
     * @return
     */
    template<typename L, typename Pred>
    awaiter<L,Pred> operator()(L &mx, Pred &&pred) {
        return awaiter<L, Pred>(*this,mx,std::forward<Pred>(pred));
    }
    
    ///notify one waiting coroutine
    /**
     * @param l specify Lockable used to protect the shared state tested by
     * predicated. Typically you lock the mutex, set a flag a call notify_one.
     * 
     * Because coroutine can be resumed at this point, you don't need to 
     * hold the mutex during this part. So if this happen, mutex
     * is unlocked, and after coroutine returns, it is locked back
     */
    template<typename L>
    void notify_one(L &l) {
        if (!_awaiters.empty()) {
            auto h = _awaiters.front();
            _awaiters.pop();
            l.unlock();
            if (!h->check_predicate()) {
                l.lock();
                _awaiters.push(h);
            } else {
                h->resume();
                l.lock();
            }
        }

    }
    ///notifty all waiting coroutine
    /**
     * @param l specify Lockable used to protect the shared state tested by
     * predicated. Typically you lock the mutex, set a flag a call notify_one.
     * 
     * Because coroutine can be resumed at this point, you don't need to 
     * hold the mutex during this part. So if this happen, mutex
     * is unlocked, and after coroutine returns, it is locked back
     */
    template<typename L>
    void notify_all(L &l) {
        std::queue<abstract_awaiter *> tmp;
        std::swap(tmp, _awaiters);
        l.unlock();
        while (!tmp.empty()) {
            auto h = _awaiters.front();
            _awaiters.pop();
            if (!h->check_predicate()) {
                std::unique_lock _(l);
                _awaiters.push(h);
            }
            h->resume();
        }
        l.lock();
    }

    ///Notify one
    /**
     * Notify one waiting coroutine.
     * If you held a mutex, you should unlock it, or use notify_one with
     * argument, where you can specify such a mutex
     */
    void notify_one() {
        empty_lock l;
        notify_one(l);
    }

    ///Notify all
    /**
     * Notify all waiting coroutine.
     * If you held a mutex, you should unlock it, or use notify_one with
     * argument, where you can specify such a mutex
     */
    void notify_all() {
        empty_lock l;
        notify_all(l);
    }

    
    ~condition_variable() {
        std::queue<abstract_awaiter *> tmp;
        _exit = true;
        std::swap(tmp, _awaiters);
        while (!tmp.empty()) {
            auto h = _awaiters.front();
            _awaiters.pop();
            h->resume();
        }
    }
    
protected:
    std::queue<abstract_awaiter *> _awaiters;
    bool _exit = false;
};



}



#endif /* SRC_COCLASSES_CONDITION_VARIABLE_H_ */
