#include <mutex>
#include <optional>
#include <queue>

#pragma once
#ifndef SRC_COCLASSES_QUEUE_H_
#define SRC_COCLASSES_QUEUE_H_

#include "common.h"
#include "exceptions.h"
#include "resume_lock.h"
#include <coroutine>

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
 */
template<typename T>
class queue {
public:
    queue() = default;
    ~queue();
    queue(const queue &) = delete;
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
    
    ///Pop an item without waiting
    /**
     * @return optional result, if there is no available item, returns empty value 
     */
    
    
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
    
    struct awaiter {
        awaiter(queue<T> &owner):_owner(owner) {}
        awaiter(const awaiter &) = default;
        awaiter &operator=(const awaiter &) = delete;
        
        bool await_ready() ;
        std::coroutine_handle<> await_suspend(handle_t h);
        T await_resume();
        queue &_owner;
        
        ///Along with pop() which returns awaiter, allows conversion to std::optional<T> for non-blocking access
        operator std::optional<T> ();
        ///Along with pop() which returns awaiter, allows conversion to T for non-blocking access
        operator T() {
            auto x = operator std::optional<T>();
            if (x.has_value()) return *x;
            else throw value_not_ready_exception();
        }
    };
    
    ///return awaiter
    awaiter operator co_await() {
        return *this;
    }
    
    awaiter pop() {
        return *this;
    }
    
    
    
protected:
    ///lock protects internal
    std::mutex _mx;
    ///queue itself
    std::queue<T> _queue;
    ///list of awaiters - in queue
    std::queue<handle_t > _awaiters;
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
inline queue<T>::awaiter::operator std::optional<T>() {
    std::optional<T> x;
    std::unique_lock lk(_owner._mx);   
    if (!_owner.empty_lk()) {
         x = (std::move(_owner._queue.front()));
        _owner._queue.pop();        
        return x;
    }
    return x;;
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
inline bool queue<T>::awaiter::await_ready()  {
    std::unique_lock lk(_owner._mx);
    if (!_owner.empty_lk()) {
        ++_owner._reserved_items;
        return true;
    } else {
        return false;
    }
    
}

template<typename T>
inline std::coroutine_handle<> queue<T>::awaiter::await_suspend(
        handle_t h) {
    std::unique_lock lk(_owner._mx);
    bool suspend = _owner.empty_lk();
    if (suspend) _owner._awaiters.push(h);
    else ++_owner._reserved_items;
    lk.unlock();
    return resume_lock::await_suspend(h, suspend);
}

template<typename T>
inline T queue<T>::awaiter::await_resume() {
    std::unique_lock lk(_owner._mx);
    if (_owner._exit) throw await_canceled_exception();
    T  x = std::move(_owner._queue.front());
    _owner._queue.pop();
    _owner._reserved_items--;
    return x;
}

template<typename T>
inline queue<T>::~queue() {
    _exit = true;
    while (!_awaiters.empty()) {
        auto x = _awaiters.front();        
        _awaiters.pop();
        x.resume();
    }
}

template<typename T>
inline void cocls::queue<T>::resume_awaiter(std::unique_lock<std::mutex> &lk) {
    if (_awaiters.empty()) return;
    handle_t h = _awaiters.front();
    _awaiters.pop();
    ++_reserved_items;
    lk.unlock();
    resume_lock::resume(h);
}
}

#endif /* SRC_COCLASSES_QUEUE_H_ */

