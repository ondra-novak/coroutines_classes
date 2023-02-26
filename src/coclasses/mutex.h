/**
 * @file mutex.h
 *
 */
#pragma once
#ifndef SRC_COCLASSES_MUTEX_H_
#define SRC_COCLASSES_MUTEX_H_

#include "awaiter.h"
#include "common.h"
#include <cassert>
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


    using awaiter = co_awaiter<mutex>;

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
        ownership() = default;
        ownership(awaiter &&awt):ownership(awt.wait()) {}
        ownership(const ownership &) = delete;
        ownership &operator=(const ownership &) = delete;
        ownership(ownership &&) = default;
        ownership &operator=(ownership &&) = default;
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
    awaiter lock() {return *this;}



    ///try lock the mutex
    /**
     * @return returns ownership object. You need to test the object
     * whether it holds ownership
     */
    ownership try_lock() {
        return is_ready()?ownership(this):ownership(nullptr);
    }


protected:

    friend class ::cocls::co_awaiter<mutex>;


    //requests to lock
    /*this is linked list in stack order LIFO, with atomic append feature */
    std::atomic<abstract_awaiter *> _requests = nullptr;
    //queue of requests, contains awaiters ordered in order of incoming
    /*this is also LIFO, but reversed - because reading LIFO to LIFO results FIFO
     * The queue is accessed under lock. It is build by unlocking thread
     * if the queue is empty by reversing _request. This is handled atomically
     */
    abstract_awaiter *_queue = nullptr;

    //when queue is build, we need object, which acts as doorman
    /*presence of doorman marks object locked. By removing doorman, object becomes unlocked */
    static constexpr abstract_awaiter *doorman() {
        return &empty_awaiter::instance;
    }

    void unlock() {
        //lock must be locked to unlock
        assert(_requests.load(std::memory_order_relaxed) != nullptr);
        //unlock operation check _queue, whether there are requests
        if (!_queue) [[likely]] {
            //if queue is empty, try to unlock. Try to replace doorman with nullptr;
            auto x = doorman();
            if (_requests.compare_exchange_strong(x, nullptr, std::memory_order_release)) [[likely]] {
                //if this passes, unlock operation is complete!
                return;
            }
            assert(x != nullptr);
            //failed, so there are awaiter
            //the queue was build above doorman (build_queue during lock)
            //so rebuild queue now (it should be empty)
            build_queue(doorman());
            //the queue is now not-empty
        }
        //pick first item from the queue
        //queue is access under lock, no atomics are needed
        abstract_awaiter *first = _queue;
        //remove item from the queue
        _queue = _queue->_next;
        //clear _next ptr to avoid leaking invalid pointer to next code
        first->_next = nullptr;
        //resume awaiter - it has ownership now
        first->resume();
        //now the _queue is also handled by the new owners
    }

    //is_ready is essentially try_lock function
    bool is_ready() {
        //we expect nullptr in _requests, try to put doorman there
        abstract_awaiter *n = nullptr;
        bool ok = _requests.compare_exchange_strong(n, doorman());
        //if ok = true, object is guarder by doorman
        return ok;
    }

    //when try_lock fails, we need to register itself to waiting queue (_requests)
    bool subscribe_awaiter(abstract_awaiter *aw) {
        //so subscribe to _requests
        aw->subscribe(_requests);
        //now check result of _next, which gives as hint, how lock operation ended
        //if the _next is null, the lock was unlock
        if (aw->_next== nullptr) [[likely]] {
            //because current awaiter will be destroyed, we need to replace self
            //with a doorman()
            //the function build_queue does this, even if there is no requests currentl
            //but they can appear inbetween. As argument set us as stop
            //use acquire memory order - obviously we acquire the mutex
            build_queue(aw);
            //suspend is not needed, we already own the mutex
            return false;
        } else {
            //we are subscribed, so continue in suspend
            return true;
        }
    }

    void build_queue(abstract_awaiter *stop) {
        assert("Can't build queue if there are items in it" && _queue == nullptr);
        //atomically swap top of _requests with doorman
        //we use acquire order - to see changes on _next
        abstract_awaiter *req = _requests.exchange(doorman(), std::memory_order_acquire);
        //if req is defined and until stop is reached
        while (req  && req != stop) {
            //pick top item, remove it and push it to _queue
            auto x = req;
            req = req->_next;
            x->_next = _queue;
            _queue= x;
        }
        //queue is updated
    }

    ownership get_result() noexcept {
        //this is called when we acquired ownership, no extract action is needed
        //just create ownership
        return ownership(this);
    }


};


}



#endif /* SRC_COCLASSES_MUTEX_H_ */
