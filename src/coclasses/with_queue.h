/** @file with_queue.h
 * 
 */

#pragma once
#ifndef _SRC_COCLASSES_WITH_QUEUE_H_qwpikqxpow
#define _SRC_COCLASSES_WITH_QUEUE_H_qwpikqxpow
#include "common.h"
#include "queue.h"


///
namespace cocls {

///Awaitable object represents current_queue of a coroutine declared as with_queue
/**
 * To await this object, just construct it and perform co_await
 * 
 * @code
 * with_queue<task<>, int> queued_coro() {
 *      co_await with_queue<task<>, int>::current_queue();
 * }
 * @endcode
 * 
 * 
 * @tparam Coro type of coroutine (task<> or generator<> )
 * @tparam T type of value 
 * 
 * @note The template arguments must match to the arguments used in coroutine declaration! 
 *  
 */
template<typename Coro, typename T>
class current_queue;

template<typename Coro, typename T>
class with_queue_promise;


///Coroutine with queue
/**
 * Implements a coroutine, which has a queue inside of its promise. You can
 * pass values to the coroutine. The coroutine can await on its internal queue.
 * 
 * Pushing to the queue is MT Safe
 * 
 * Coroutine need to await current_queue
 * 
 * @tparam Coro type of coroutine (task<> or generator<>)
 * @tparam T type of item in queue
 */
template<typename Coro, typename T>
class with_queue: public Coro {
public:
    
    using promise_type = with_queue_promise<Coro, T>;
    ///awaitable object, which represents a queue associated with current task
    /**
     * You need to just create it and immediatelly co_await it
     */
    using current_queue = ::cocls::current_queue<Coro, T>;   
    with_queue(Coro &&x):Coro(std::move(x)) {}
    with_queue() {}
    
    using Coro::Coro;

    ///Push a value to the coroutine
    /**
     * @param t value to push. Function is MT Safe, so multiple threads can push to the queue
     * 
     */
    void push(T &&t) {
        static_cast<with_queue_promise<Coro, T> *>(this->get_promise())->push(std::move(t));
    }
    ///Push a value to the coroutine
    /**
     * @param t value to push. Function is MT Safe, so multiple threads can push to the queue
     * 
     */
    void push(const T &t) {
        static_cast<with_queue_promise<Coro, T> *>(this->get_promise())->push(t);
    }
    
};


template<typename Coro, typename T>
class with_queue_promise: public Coro::promise_type {
public:
    
    using queue_t = queue<T, std::queue<T>, primitives::single_item_queue<abstract_awaiter<> *> >; 
    
    queue_t _q;
    
    void push(T &&t) {
        _q.push(std::move(t));
    }
    void push(const T &t) {
        _q.push(t);
    }

    
    with_queue<Coro,T> get_return_object() {
        return with_queue<Coro,T>(std::move(Coro::promise_type::get_return_object()));
    }
    
    
};

template<typename Coro ,typename T>
class current_queue {
public:
    using promise_t = with_queue_promise<Coro, T>;
    using queue_t = typename promise_t::queue_t;
    using awaiter_t = decltype(std::declval<queue_t>().pop());
    static bool await_ready() noexcept {return false;}
    bool await_suspend(std::coroutine_handle<> h) {
        _h = std::coroutine_handle<promise_t>::from_address(h.address());
        auto &promise = _h.promise();
        _awaiter.emplace(std::move(promise._q.pop()));
        if (_awaiter->await_ready()) {
            return false;
        }
        return _awaiter->await_suspend(h);
    }
    T await_resume() {
        return _awaiter->await_resume();
    }
protected:
    std::coroutine_handle<promise_t> _h;
    std::optional<awaiter_t> _awaiter;
};

template<typename Coro ,typename T>
struct current_queue<with_queue<Coro, T>, T>: current_queue<Coro, T> {};


}




#endif /* SRC_COCLASSES_WITH_QUEUE_H_ */
