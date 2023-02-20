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

namespace primitives {
    template<typename X> class std_queue;
}

///Awaitable queue
/**
 * @tparam T type of queued item
 * @tparam Queue type which implements queue of T objects, default is std::queue<T>
 * @tparam CoroQueue type which implements queue of waiting awaiter, default is std::queue<abstract_awaiter *>
 * @tparam Lock object responsible to lock internals - default is std::mutex
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
template<typename T,
         typename Queue = primitives::std_queue<T>,
         typename CoroQueue = primitives::std_queue<abstract_awaiter*>,
         typename Lock = std::mutex >
class queue {
public:
    ///construct empty queue
    queue() = default;
    ~queue();

    ///Push the item
    /**
     * @param x rvalue reference for item, use for movable items
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

    bool subscribe_awaiter(abstract_awaiter *aw) {
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
    Lock _mx;
    ///queue itself
    Queue _queue;
    ///list of awaiters - in queue
    CoroQueue _awaiters;
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

    std::size_t size_lk() const {
        return _queue.size() - _reserved_items;
    }
    bool empty_lk() const {
        return size_lk() == 0;
    }
};

///Queue specialization with void as main type
/**
 * This queue works like event counter. When you push a void value, it resumes
 * awaiting coroutine. If there is no such coroutine, it just counts +1 and this
 * value is remove when coroutine tries to co_await. Works similar to queue with
 * a value, but no values are exchanges
 * @tparam Queue Queue implementation. Default value implements queue<void> which
 * is specialised as counter without memory allocation - because no value is
 * needed to be stored
 * @tparam CoroQueue queue implementation for awaiters
 * @tparam Lock object
 */
template<typename Queue,typename CoroQueue,typename Lock>
class queue<void, Queue, CoroQueue, Lock> {
public:
    ///construct empty queue
    queue() = default;
    ~queue();

    ///Push an event
    void push();
    ///determines, whether there are no events
    /**
     *
     * @retval true no events in queue
     * @retval false at least one event in the queue
     */
    bool empty();
    ///Count of events in queue
    /**
     * @return count of events in the queue
     */
    std::size_t size();
    ///Pops from the queue
    /**
     * @return returns awaiter. You need to co_await on result. Note that
     * result of co_await is void, it just handles suspension and resumption
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

    bool subscribe_awaiter(abstract_awaiter *aw) {
        std::unique_lock lk(_mx);
        bool suspend = empty_lk();
        if (suspend) _awaiters.push(aw);
        else ++_reserved_items;
        lk.unlock();
        return suspend;

    }

    void get_result() {
        std::unique_lock lk(_mx);
        if (_exit) throw await_canceled_exception();
        _queue.pop();
        _reserved_items--;

    }


    friend class co_awaiter<queue>;
    ///lock protects internal
    Lock _mx;
    ///queue itself
    Queue _queue;
    ///list of awaiters - in queue
    CoroQueue _awaiters;
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

    std::size_t size_lk() const {
        return _queue.size() - _reserved_items;
    }
    bool empty_lk() const {
        return size_lk() == 0;
    }
};

namespace primitives {

    ///Simulates queue interface above single item.
    /** It can be used to simplify queue of awaiters for queue<>, if only
     * one coroutine is expected to be awaiting. However if this
     * promise is not fullfiled, the result is UB
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
        void push(T&& val) {
            emplace(std::move(val));
        }
        ///
        void push(const T& val) {
            emplace(val);
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

    template<typename X>
    class std_queue: public std::queue<X> {
    public:
        using std::queue<X>::queue;
    };

    template<>
    class std_queue<void> {
    public:
        void push() {++_sz;}
        void pop() {_sz = std::max<std::size_t>(1, _sz)-1;}
        std::size_t size() const {return _sz;}
        bool empty() const {return _sz == 0;}
    protected:
        std::size_t _sz = 0;

    };
}

template<typename T, typename Queue, typename CoroQueue, typename Lock>
inline void queue<T,Queue,CoroQueue, Lock>::push(T &&x) {
    std::unique_lock<std::mutex> lk(_mx);
    //push to queue under lock
    _queue.push(std::move(x));
    //resume any awaiter - need lk to unlock before resume
    resume_awaiter(lk);
}

template<typename T, typename Queue, typename CoroQueue, typename Lock>
inline void queue<T,Queue,CoroQueue, Lock>::push(const T &x) {
    std::unique_lock<std::mutex> lk(_mx);
    //push to queue under lock
    _queue.push(x);
    //resume any awaiter - need lk to unlock before resume
    resume_awaiter(lk);
}


template<typename T, typename Queue, typename CoroQueue, typename Lock>
inline bool queue<T,Queue,CoroQueue, Lock>::empty() {
    std::unique_lock lk(_mx);
    return empty_lk();
}

template<typename T, typename Queue, typename CoroQueue, typename Lock>
inline std::size_t queue<T,Queue,CoroQueue, Lock>::size() {
    std::unique_lock lk(_mx);
    return size_lk();
}



template<typename T, typename Queue, typename CoroQueue, typename Lock>
inline queue<T,Queue,CoroQueue, Lock>::~queue() {
    _exit = true;
    while (!_awaiters.empty()) {
        auto x = _awaiters.front();
        _awaiters.pop();
        x->resume();
    }
}

template<typename T, typename Queue, typename CoroQueue, typename Lock>
inline void cocls::queue<T,Queue,CoroQueue, Lock>::resume_awaiter(std::unique_lock<std::mutex> &lk) {
    if (_awaiters.empty()) return;
    auto h = _awaiters.front();
    _awaiters.pop();
    ++_reserved_items;
    lk.unlock();
    h->resume();
}



template<typename Queue, typename CoroQueue, typename Lock>
inline void queue<void,Queue,CoroQueue, Lock>::push() {
    std::unique_lock<std::mutex> lk(_mx);
    //push to queue under lock
    _queue.push();
    //resume any awaiter - need lk to unlock before resume
    resume_awaiter(lk);
}


template< typename Queue, typename CoroQueue, typename Lock>
inline bool queue<void,Queue,CoroQueue, Lock>::empty() {
    std::unique_lock lk(_mx);
    return empty_lk();
}

template<typename Queue, typename CoroQueue, typename Lock>
inline std::size_t queue<void,Queue,CoroQueue, Lock>::size() {
    std::unique_lock lk(_mx);
    return size_lk();
}



template<typename Queue, typename CoroQueue, typename Lock>
inline queue<void ,Queue,CoroQueue, Lock>::~queue() {
    _exit = true;
    while (!_awaiters.empty()) {
        auto x = _awaiters.front();
        _awaiters.pop();
        x->resume();
    }
}

template<typename Queue, typename CoroQueue, typename Lock>
inline void cocls::queue<void,Queue,CoroQueue, Lock>::resume_awaiter(std::unique_lock<std::mutex> &lk) {
    if (_awaiters.empty()) return;
    auto h = _awaiters.front();
    _awaiters.pop();
    ++_reserved_items;
    lk.unlock();
    h->resume();
}


}



#endif /* SRC_COCLASSES_QUEUE_H_ */

