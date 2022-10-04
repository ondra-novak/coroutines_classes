/*
 * mutex.h
 *
 *  Created on: 4. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_MUTEX_H_
#define SRC_COCLASSES_MUTEX_H_

#include "common.h"
#include "resume_lock.h"

#include <cassert>
#include <condition_variable>
#include <coroutine>
#include <memory>
#include <mutex>

namespace cocls {

///simple mutex for coroutines
/**
 * when mutex is owned, other coroutines are suspended and when mutex is unlocked, one
 * of waiting coroutine is resumed.
 * 
 * This mutex can be also used by non-coroutine, but this suspends whole thread
 * 
 * @code
 * cocls::mutex mx;
 * auto ownership = co_await mx;  //lock the mutex
 * //mutex is unlocked, when ownership is destroyed
 * //or ownership is released: ownership.release()
 * @endcode 
 * 
 * In contrary to classical mutex, releasing the ownership can cause suspend of current
 * function in favor to awaiting coroutine. So manage ownership carefully to avoid
 * unexpected interruption of your code. You can release ownership in different thread, which
 * causes that coroutine will continue in that thread 
 * 
 * 
 */
class mutex {
protected:    
    class ownership_deleter {
    public:
        void operator()(mutex *mx) {
            mx->unlock();
        }
    };
    

    
    
    class abstract_awaiter {
    public:
        abstract_awaiter() = default;
        virtual ~abstract_awaiter() = default;

        virtual void resume() noexcept = 0;
        abstract_awaiter *next = nullptr;
    protected:
    };

    class null_awaiter: public abstract_awaiter {
    public:
        virtual void resume() noexcept {};
    };
    
    class sync_awaiter: public abstract_awaiter {
    public:
        
        void wait() {
            std::unique_lock _(_mx);
            _cond.wait(_,[this]{return _signal;});
        }
        virtual void resume() noexcept {
            std::unique_lock _(_mx);
            _signal = true;
            _cond.notify_one();
        }
    protected:
        std::mutex _mx;
        std::condition_variable _cond;
        bool _signal = false;
    };
    

    
    

public:
    using ownership = std::unique_ptr<mutex, ownership_deleter>;

    class awaiter: public abstract_awaiter {
    public:
        awaiter(mutex &owner):_owner(owner) {}
        awaiter(const awaiter &other) = default;
        awaiter &operator=(const awaiter &other) = delete;
        bool await_ready() noexcept {return _owner.await_ready();}        
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
            _h = h;
            return resume_lock::await_suspend(h, _owner.await_suspend(this));
        }
        ownership await_resume() noexcept {
            return _owner.await_resume();
        }
        virtual void resume() noexcept override {
            resume_lock::resume(_h);
        }
        
    protected:
        std::coroutine_handle<> _h;
        mutex &_owner;
        
    };


public:   
    mutex() = default;
    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;
    ~mutex() {
        //when mutex is destroyed, check whether there is no one waiting on it
        assert(_queue == nullptr);
        assert(_requests == nullptr);
    }


    ownership lock();
    
    awaiter operator co_await() {return *this;}

    void unlock();
    
    ownership try_lock();
    
    
    
protected:
    
    std::atomic<abstract_awaiter *> _requests = nullptr;
    abstract_awaiter *_queue = nullptr;
    null_awaiter _locked;
    
    bool await_ready() {
        abstract_awaiter *n = nullptr;
        bool ok = _requests.compare_exchange_strong(n, &_locked);
        return ok;
    }
    
    bool await_suspend(abstract_awaiter *aw) {
        while (!_requests.compare_exchange_weak(aw->next,aw));
        if (aw->next == nullptr) {
            //as we are subscribe into queue, we expect that aw->next is &_locked
            //however if it is nullptr, lock has been unlocked at the time
            //and because the awaiter will disappear, exchange it by _locked 
            //but note, some other awaiters can alredy be behind us
            //fortunately - lock is locked for us, so we can use unprotected variables
            //to solve this issue
            build_queue(aw);
            //suspend is not needed, we already own the mutex
            return false;
        } else {
            //we are subscribed, so continue in suspend
            return true;
        }
    }
    
    void build_queue(abstract_awaiter *stop) {
        //queue is linked list of awaiters order by order of income
        //queue is under control of owned lock
        //can be modified only by owner
        
        abstract_awaiter **tail = &_queue;
        //try to find end of queue - tail points to end pointer
        while (*tail) { //if the end pointer is not null, move to next item 
            tail = &((*tail)->next);
        }
        //pick requests - so the list no longer can be modified
        //because mutex should be locked, exchange it with _locked flag
        abstract_awaiter *reqs = _requests.exchange(&_locked);
        //requests are ordered in reverse order, so process it and update queue
        //stop on nullptr or on _locked
        while (reqs && reqs != stop) {
            auto n = reqs;
            reqs = reqs->next;
            n->next = nullptr; //this item will be at the end
            *tail = n;  //append to tail
            tail = &(n->next); //move tail to the end
            
        }
        //all done, queue updated
    }
    
    ownership await_resume() noexcept {
        return ownership(this); //this is easy, just create smart pointer and return it
    }
    

};

inline mutex::ownership mutex::lock() {
    //first try-lock, it is fastest
    if (await_ready()) {
        //as we acquired lock, return ownership
        return ownership(this);
    }
    //create synchronous awaiter
    sync_awaiter aw;
    //register awaiter - if true returned
    if (await_suspend(&aw)) {
        //we can wait for the awaiter (wait for resume)
        aw.wait();
    }   //if false, ownership has been aquired, so awaiter is no longer needed
    return await_resume();
}

inline void mutex::unlock() {
        bool rep;
        do {
            rep = false;
            //unlock must be called from the owner. Because lock is owned
            //owner has access to the queue
            //check queue now - the queue is under our control
            if (_queue) {   //some items are in queue, so release 
                auto *a = _queue;   //pick first item
                _queue = _queue->next;  //remove it from the queue
                a->resume(); //resume this item - so now, ownership is transfered
            } else { //no items in queue?
                //assume, no requests
                abstract_awaiter *n = &_locked;
                //try to mark lock unlocked
                if (!_requests.compare_exchange_strong(n, nullptr)) {
                    //attempt was unsuccessful, there are requests
                    //build queue now
                    build_queue(&_locked);
                    //repeat unlock
                    rep = true;                          
                }
                
            }
        } while (rep);
        //all done
}

inline mutex::ownership mutex::try_lock() {
    return ownership(await_ready()?this:nullptr);
}

}



#endif /* SRC_COCLASSES_MUTEX_H_ */
