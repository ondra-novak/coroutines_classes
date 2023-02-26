/**
 * @file queue.h
 */
#pragma once
#ifndef SRC_COCLASSES_QUEUE_H_
#define SRC_COCLASSES_QUEUE_H_

#include "common.h"
#include "exceptions.h"
#include "future.h"

#include <coroutine>

#include <mutex>
#include <optional>
#include <queue>


namespace cocls {

namespace primitives {
    /// represents empty lock, no-lock, object which simulates locking but doesn't lock at all
    /** You can use it if you need to remove often cost operation of locking in othewise single
     * thread use
     */
    class no_lock {
    public:
        void lock() {}
        void unlock() {}
        bool try_lock() { return true; }
    };

    ///wrapped std::queue, however this queue works for <void>
    template<typename X>
    class std_queue: public std::queue<X> {
    public:
        using std::queue<X>::queue;
    };

    ///template specialization for <void>
    /**
     * Such queue doesn't stores items, it just count push
     * and pops, very similar as semaphore.
     */
    template<>
    class std_queue<void> {
    public:
        void emplace() {++_sz;}
        void pop() {_sz = std::max<std::size_t>(1, _sz)-1;}
        std::size_t size() const {return _sz;}
        bool empty() const {return _sz == 0;}
    protected:
        std::size_t _sz = 0;

    };

    ///Simulates queue interface above single item.
    /** It can be used to simplify queue of awaiters for queue<>, if only
     * one coroutine is expected to be awaiting. However if this
     * promise is not fulfilled, the result is UB
     *
     *
     * @tparam T type of item
     */
    template<typename T>
    class single_item_queue {
    public:

        ///
        void clear() { !_val.reset(); }
        ///
        bool empty() const { return !_val.has_value(); }

        ///
        template<typename ... Args>
        void emplace(Args && ... args) {
            if (_val.has_value()) throw std::runtime_error("Single item queue is full");
            _val.emplace(std::forward<Args>(args)...);
        }
        ///
        std::size_t size() const { return empty() ? 0 : 1; }
        ///
        T& front() { return *_val; }
        ///
        const T& front() const { return *_val; }
        ///
        void pop() {
            _val.reset();
        }

    protected:
        std::optional<T> _val;
    };
}


///Awaitable queue - unlimited
/**
 * Implementation of thread safe, multiple producent multiple consument queue.
 *
 * @tparam T type of queued item
 * @tparam Queue template which implements queue for items. The template accepts single argument
 * @tparam CoroQueue template which implements queue for awaiting consuments. The template accepts single argument.
 * You can change this argument to primitives::single_item_queue, to request single consument and save some memory
 * and performance
 * @tparam Lock object responsible to lock internals - default is std::mutex
 *
 * Awaitable queue is queue, which can be awaited for the new item. If there is no
 * item in the queue, the awaiting coroutine is suspened. Pushing new item into
 * queue causes resumption of the coroutine
 *
 * The implementation is MT Safe - for the purpose of this object. So it is allowed
 * to push items from multiple threads without any extra synchronizations. It
 * is also possible to have multiple awaiting coroutines (unless single_item_queue is used
 * for CoroQueue)
 *
 *
 * @code
 * queue<int> q;
 *
 * q.push(42);
 *
 * int value = co_await q;
 * @endcode
 */
template<typename T,
         template<typename> class Queue =  primitives::std_queue,
         template<typename> class CoroQueue =  primitives::std_queue,
         typename Lock = std::mutex >
class queue {
public:
    ///construct empty queue
    queue() = default;

    ///Push the item
    /**
     * @param x rvalue reference for item, use for movable itesm
     *
     * @note if there is awaiting coroutine, it may be resumed now (resume_lock is used)
     */
    template<typename ... Args>
    void push(Args && ... args) {
        std::unique_lock lk(_mx);
        if (!_awaiters.empty()) {
            promise<T> p = std::move(_awaiters.front());
            _awaiters.pop();
            lk.unlock();
            p(std::forward<Args>(args)...);
        } else {
            _queue.emplace(std::forward<Args>(args)...);
        }

    }

    ///Determines, whether queue is empty
    /**
     * @retval true queue is empty
     * @retval false queue is not empty
     */
    bool empty() {
        std::lock_guard _(_mx);
        return _queue.empty();
    }

    ///Retrieves count of waiting items
    /**
     * @return count of waiting items
     */
    std::size_t size() {
        std::lock_guard _(_mx);
        return _queue.size();
    }



    ///pop the item from the queue
    /**
     * @return awaiter which can be awaited
     *
     * @code
     * queue<int> q;
     *
     *   //async
     * int val1 = co_await q.pop();
     *
     *   //sync
     * int val2 = q.pop().wait();
     * @endcode
     */
    future<T> pop() {
        return [&](auto promise) {
            std::unique_lock lk(_mx);
            if (_queue.empty()) {
                _awaiters.emplace(std::move(promise));
            } else {
                if constexpr(!std::is_void_v<T>) {
                    promise(std::move(_queue.front()));
                } else {
                    promise();
                }
                _queue.pop();
                lk.unlock();
            }
        };
    }

    ///unblock awaiting coroutine which awaits on pop() with an exception
    /**
     * Useful to implement timeouts
     *
     * @param e exception to be set as result of unblocking
     * @retval true success
     * @retval false nobody is awaiting
     */
    bool unblock_pop(std::exception_ptr e) {
        std::unique_lock lk(_mx);
        if (_awaiters.empty()) return false;
        promise<T> p = std::move(_awaiters.front());
        _awaiters.pop();
        lk.unlock();
        p.set_exception(e);
        return true;
    }


protected:
    Lock _mx;
    ///queue itself
    Queue<T> _queue;
    ///list of awaiters - in queue
    CoroQueue<promise<T> > _awaiters;
};

///Awaitable queue - limited
/**
 *
 * Limited queue has specified limit. Once the count of items exeedes the limit, caller
 * is blocked, waiting to some items be removed. The function push() return promise
 * which must be awaited.
 *
 * @tparam T type of item
 * @tparam Queue template implementing queue for items
 * @tparam CoroQueue template implementing queue for consuments
 * @tparam BlockedQueue template implementing queue for producers
 * @tparam Lock implementation of lock
 */
template<typename T,
         template<typename> class Queue =  primitives::std_queue,
         template<typename> class CoroQueue =  primitives::std_queue,
         template<typename> class BlockedQueue = primitives::std_queue,
         typename Lock = std::mutex >
class limited_queue: protected queue<T, Queue, CoroQueue, Lock> {
public:
    limited_queue(std::size_t limit):_limit(limit) {}

    ///Push item, returns future
    /**
     * @param args arguments to construct an item in the queue.
     * @return future, which must be co_awaited, or synced. Discarding
     * this result is error and can lead to UB (the compiler should generate warning)
     *
     * The future is returned resolved, when the insert was successful, or unresolved
     * when the caller must wait for insertion.
     *
     */
    template<typename ... Args>
    future<void> push(Args && ... args) {
        std::unique_lock lk(this->_mx);
        if (!this->_awaiters.empty()) {
            promise<T> p = std::move(this->_awaiters.front());
            this->_awaiters.pop();
            lk.unlock();
            p(std::forward<Args>(args)...);
            return future<void>::set_value();
        } else {
            this->_queue.emplace(std::forward<Args>(args)...);
            if (this->_queue.size() >= _limit) {
                return [&](auto promise) {
                    _blocked.push({T(std::forward<Args>(args)...),std::move(promise)});
                };
            } else {
                return future<void>::set_value();
            }
        }
    }

    using queue<T, Queue, CoroQueue, Lock>::size;
    using queue<T, Queue, CoroQueue, Lock>::empty;

    ///Pops item returns promise
    future<T> pop() {
        return [&](auto promise) {
            std::unique_lock lk(this->_mx);
            if (this->_queue.empty()) {
                this->_awaiters.emplace(std::move(promise));
            } else {
                if constexpr(!std::is_void_v<T>) {
                    promise(std::move(this->_queue.front()));
                } else {
                    promise();
                }
                this->_queue.pop();
                if (!_blocked.empty()) {
                    auto front = std::move(_blocked.front());
                    this->_queue.push(std::move(front.first));
                    auto p = std::move(front.second);
                    _blocked.pop();
                    lk.unlock();
                    p();
                } else {
                    lk.unlock();
                }
            }
        };
    }


    ///unblock first awaiting coroutine which awaits on push() with an exception
    /**
     * Useful to implement timeouts
     *
     * @param e exception to be set as result of unblocking
     * @retval true success
     * @retval false nobody is awaiting
     *
     * @note unblocking push operation also removes the item
     *
     *
     */
    bool unblock_push(std::exception_ptr e) {
        std::unique_lock lk(this->_mx);
        if (_blocked.empty()) return false;
        auto front = std::move(_blocked.front());
        _blocked.pop();
        lk.unlock();
        front.second.set_exception(e);
        return true;
    }

protected:
    BlockedQueue<std::pair<T, promise<void> > > _blocked;
    std::size_t _limit;
};


}



#endif /* SRC_COCLASSES_QUEUE_H_ */

