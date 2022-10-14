#pragma once
#ifndef SRC_COCLASSES_WITH_QUEUE_H_
#define SRC_COCLASSES_WITH_QUEUE_H_
#include "queue.h"

#ifndef NDEBUG
#include <cstring>
#include <cassert>
#endif

namespace cocls {

///Awaitable object represents current_queue of a coroutine declared as with_queue
/**
 * To await this object, just construct it and perform co_await
 * 
 * @code
 * co_await current_queue<Coro, T>()
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
    
    queue<T> _q;
    
    void push(T &&t) {
        _q.push(std::move(t));
    }
    void push(const T &t) {
        _q.push(t);
    }

    
    with_queue<Coro,T> get_return_object() {
        return with_queue<Coro,T>(std::move(Coro::promise_type::get_return_object()));
    }
    
#ifndef NDEBUG
    const char *queue_name = typeid(current_queue<Coro, T>).name();
#endif
    
};

template<typename Coro ,typename T>
class current_queue {
public:
    using promise_t = with_queue_promise<Coro, T>;
    static bool await_ready() noexcept {return false;}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        _h = std::coroutine_handle<promise_t>::from_address(h.address());
        auto &promise = _h.promise();
#ifndef NDEBUG
        const char *qn = typeid(current_queue<Coro, T>).name();
        assert(std::strcmp(qn, promise.queue_name) == 0); //invalid queue
#endif
        _awaiter.emplace(std::move(promise._q.pop()));
        if (_awaiter->await_ready()) {
            return h;
        }
        return _awaiter->await_suspend(h);
    }
    T await_resume() {
        return _awaiter->await_resume();
    }
protected:
    std::coroutine_handle<promise_t> _h;
    std::optional<co_awaiter<queue<T> >> _awaiter;
};

template<typename Coro ,typename T>
struct current_queue<with_queue<Coro, T>, T>: current_queue<Coro, T> {};


}




#endif /* SRC_COCLASSES_WITH_QUEUE_H_ */
