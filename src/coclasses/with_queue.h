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

template<typename Coro, typename T>
class with_queue_promise;


///Coroutine with queue
/**
 * Implements a coroutine, which has a queue inside of its promise. You can
 * pass values to the coroutine. The coroutine can await on its internal queue.
 *
 * Pushing to the queue is MT Safe
 *
 * The coroutine use co_yield {} to wait and read item from the queue.
 *
 * @tparam Coro type of coroutine (task<> or generator<>)
 * @tparam T type of item in queue
 */
template<typename Coro, typename T>
class with_queue: public Coro {
public:

    using promise_type = with_queue_promise<Coro, T>;
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

    auto yield_value(std::monostate) {
        return _q.pop();
    }


};


}




#endif /* SRC_COCLASSES_WITH_QUEUE_H_ */
