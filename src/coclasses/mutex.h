/**
 * @file mutex.h
 *
 */
#pragma once
#ifndef SRC_COCLASSES_MUTEX_H_
#define SRC_COCLASSES_MUTEX_H_

#include "common.h"
#include "abstract_awaiter.h"

#include <cassert>
#include <coroutine>
#include <memory>
#include <mutex>
#include <condition_variable>


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
 * auto ownership = co_await mx.lock();  //lock the mutex
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
    
    
    

public:


    using co_awaiter = ::cocls::co_awaiter<mutex,true>;
    using blocking_awaiter = ::cocls::blocking_awaiter<mutex, true>;
    using abstract_awaiter = ::cocls::abstract_awaiter<true>; 
    class null_awaiter: public abstract_awaiter {
        virtual void resume()  noexcept override {}
       
    };

    ///construct a mutex
    /**
     * Mutex can't be copied or moved
     */
    mutex() {}
    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;
    ~mutex() {
        //when mutex is destroyed, check whether there is no one waiting on it
        assert(_queue == nullptr);
        assert(_requests == nullptr);
    }

    ///Contains ownership of the mutex
    /** By holding this object, you owns an ownership */
    class ownership {
    public:
        ownership(co_awaiter &&awt):ownership(awt.wait()) {}
        ownership(ownership &&) = default;
        ownership &operator=(ownership &&) = delete;
        ///Release ownership
        /**
         * From now, ownership is not held, even if you still have the object
         */
        void release() {_ptr.reset();}
        
        ///Returns true, if you still owns the mutex (not released)
        /**
         * @retval true ownership is still held
         * @retval false ownership has been released
         */
        operator bool() const {return _ptr != nullptr;}
        ///Returns true, if ownership has been released
        /**
         * @retval false ownership is still held
         * @retval true ownership has been released
         */
        bool operator !() const {return _ptr == nullptr;}
    protected:
        ownership(mutex *mx):_ptr(mx) {}
        friend class mutex;
        std::unique_ptr<mutex, ownership_deleter> _ptr; 
    };

    ///lock the mutex, obtain the ownership
    /**
     * @return ownership. It is always held until it is released
     * 
     * @note function must be called with co_await. You can also use wait()
     * to obtain ownership outside of coroutine
     */
    co_awaiter lock() {return co_awaiter(*this);}
    
    
    
    ///try lock the mutex
    /**
     * @return returns ownership object. You need to test the object
     * whether it holds ownership
     */
    ownership try_lock() {
        return is_ready()?ownership(this):ownership(nullptr);
    }
    
    
protected:
    
    friend class ::cocls::blocking_awaiter<mutex, true>;
    friend class ::cocls::co_awaiter<mutex, true>;
    
    
    std::atomic<abstract_awaiter *> _requests = nullptr;
    abstract_awaiter *_queue = nullptr;
    null_awaiter _locked;
    
    void unlock() {
            bool rep;
            do {
                rep = false;
                //unlock must be called from the owner. Because lock is owned
                //owner has access to the queue
                //check queue now - the queue is under our control
                if (_queue) {   //some items are in queue, so release 
                    auto *a = _queue;   //pick first item
                    _queue = _queue->_next;  //remove it from the queue
                    a->resume();
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

    bool is_ready() {
        abstract_awaiter *n = nullptr;
        bool ok = _requests.compare_exchange_strong(n, &_locked);
        return ok;
    }
    
    bool subscribe_awaiter(abstract_awaiter *aw) {
        aw->subscribe(_requests);
        if (aw->_next== nullptr) {
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
            tail = &((*tail)->_next);
        }
        //pick requests - so the list no longer can be modified
        //because mutex should be locked, exchange it with _locked flag
        abstract_awaiter *reqs = _requests.exchange(&_locked);
        //requests are ordered in reverse order, so process it and update queue
        //stop on nullptr or on _locked
        while (reqs && reqs != stop) {
            auto n = reqs;
            reqs = reqs->_next;
            n->_next = nullptr; //this item will be at the end
            *tail = n;  //append to tail
            tail = &(n->_next); //move tail to the end
            
        }
        //all done, queue updated
    }
    
    ownership get_result() noexcept {
        return ownership(this); //this is easy, just create smart pointer and return it
    }
    

};


}



#endif /* SRC_COCLASSES_MUTEX_H_ */
