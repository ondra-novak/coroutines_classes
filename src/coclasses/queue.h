/**
 * @file queue.h
 */
#pragma once
#ifndef SRC_COCLASSES_QUEUE_H_
#define SRC_COCLASSES_QUEUE_H_

#include "awaiter.h"
#include "common.h"
#include "exceptions.h"
#include "resumption_policy.h"
#include "queued_resumption_policy.h"

#include <coroutine>

#include <mutex>
#include <optional>
#include <queue>

namespace cocls {

///Awaitable queue
/**
 * @tparam T type of queued item
 * 
 * Awaitable queue is queue, which can be awaited for the new item. If there is no
 * item in the queue, the awaiting coroutine is suspened. Pushing new item into
 * queue causes resumption of the coroutine
 * 
 * The implementation is MT Safe - for the purpose of this object. So it is allowed
 * to push items from multiple threads without any extra synchronizations. It
 * is also possible to have multiple awaiting coroutines
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
template<typename T>
class queue {
public:
    ///construct empty queue
    queue() = default;
    ~queue();
    ///Queue can't be copied
    queue(const queue &) = delete;
    ///Queue can't be moved
    queue &operator=(const queue &) = delete;
    
    ///Push the item
    /**
     * @param x rvalue reference for item, use for movable itesm
     * 
     * @note if there is awaiting coroutine, it may be resumed now (resume_lock is used)
     */
    void push(T &&x);
    
    ///Push the item
    /**
     * @param x rvalue reference for item, use for movable itesm
     * 
     * @note if there is awaiting coroutine, it may be resumed now (resume_lock is used)
     */
    void push(const T &x);
    
    ///Determines, whether queue is empty
    /**
     * @retval true queue is empty
     * @retval false queue is not empty
     */
    bool empty();
    
    ///Retrieves count of waiting items
    /**
     * @return count of waiting items
     */
    std::size_t size();
    
    
    
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
    co_awaiter<queue> pop() {
        return *this;
    }
    
    
    
protected:
    
    bool is_ready() {
        std::unique_lock lk(_mx);
        if (!empty_lk()) {
            ++_reserved_items;
            return true;
        } else {
            return false;
        }
    }
    
    bool subscribe_awaiter(abstract_awaiter<> *aw) {
        std::unique_lock lk(_mx);
        bool suspend = empty_lk();
        if (suspend) _awaiters.push(aw);
        else ++_reserved_items;
        lk.unlock();
        return suspend;
        
    }

    T get_result() {
        std::unique_lock lk(_mx);
        if (_exit) throw await_canceled_exception();
        T  x = std::move(_queue.front());
        _queue.pop();
        _reserved_items--;
        return x;
    }

    
    friend class co_awaiter<queue>;
    ///lock protects internal
    std::mutex _mx;
    ///queue itself
    std::queue<T> _queue;
    ///list of awaiters - in queue
    std::queue<abstract_awaiter<> *> _awaiters;
    ///count of items in the queue reserved for return
    /**
     * once coroutine is being resumed, the item, which is going to be returned
     * must be stored somewhere. The queue is used for this purpose, so the
     * item don't need to be copied during this process. It is only reserver and
     * removed once the resumption is completed. 
     * 
     *   
     */
    std::size_t _reserved_items = 0;
    
    bool _exit = false;;
    
    void resume_awaiter(std::unique_lock<std::mutex> &lk);
    
    bool size_lk() const {
        return _queue.size() - _reserved_items;
    }
    bool empty_lk() const {
        return size_lk() == 0;
    }
};



template<typename T>
inline void queue<T>::push(T &&x) {
    std::unique_lock<std::mutex> lk(_mx);
    //push to queue under lock
    _queue.push(std::move(x));
    //resume any awaiter - need lk to unlock before resume
    resume_awaiter(lk);
}

template<typename T>
inline void queue<T>::push(const T &x) {
    std::unique_lock<std::mutex> lk(_mx);
    //push to queue under lock
    _queue.push(x);
    //resume any awaiter - need lk to unlock before resume
    resume_awaiter(lk);
}


template<typename T>
inline bool queue<T>::empty() {
    std::unique_lock lk(_mx);
    return empty_lk();
}

template<typename T>
inline std::size_t queue<T>::size() {
    std::unique_lock lk(_mx);
    return size_lk();
}



template<typename T>
inline queue<T>::~queue() {
    _exit = true;
    while (!_awaiters.empty()) {
        auto x = _awaiters.front();        
        _awaiters.pop();
        x->resume();
    }
}

template<typename T>
inline void cocls::queue<T>::resume_awaiter(std::unique_lock<std::mutex> &lk) {
    if (_awaiters.empty()) return;
    auto h = _awaiters.front();
    _awaiters.pop();
    ++_reserved_items;
    lk.unlock();
    h->resume();
}
}

#endif /* SRC_COCLASSES_QUEUE_H_ */

