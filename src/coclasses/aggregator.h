#pragma once
#ifndef SRC_COCLASSES_AGGREGATOR_H_
#define SRC_COCLASSES_AGGREGATOR_H_

#include "common.h"

#include "resume_lock.h"

#include <coroutine>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace cocls {


///Aggegator, aggregates events, co_awaitable object
/**
 * @tparam Key specifies type of key, identifier of source
 * @tparam Value specifies type of value
 * 
 * You can create callbacks, which, when they are called, the awaiting 
 * coroutine is resumed. It is safe to post various values from multiple
 * threads. If the consuming coroutine is not fast enough, event is stored 
 * until they are read.
 * 
 * Result of this aggregation is <Key, Value> where Key is identifier of source
 * and Value is actual value
 */
template<typename Key, typename Value>
class aggregator {
public:    
    
    ///Contains source and value
    using kv_type = std::pair<Key, Value>;
    
    ///count of pending events
    /**
     * @return count of pending unprocessed events, including pending 
     */
    std::size_t size() const {
        std::lock_guard _(_mx);
        return _count + _ready.size();
        
    }
    ///returns true, when no more events are expected
    bool empty() const {        
        return size() == 0;
    }
    
    ///Post value directly
    /**
     * @param kv key and value
     */
    void push(kv_type &&kv) {
        std::coroutine_handle<> h;
        {
            std::lock_guard _(_mx);
            _count--;
            _ready.push(std::move(kv));
            h = _suspended;
            _suspended = nullptr;
        }
        if (h) {
            resume_lock::resume(h);
        }
        
    }
    ///Construct callback, a function which can be called with a value
    /**
     * @param k identifier of source
     * @return callback
     */
    auto make_callback(const Key &k) {
        std::unique_lock _(_mx);
        _count++;
        return [k, this](Value v) mutable {
            push({std::move(k),std::move(v)});
        };
    }
    ///Construct callback, a function which can be called with a value
    /**
     * @param k identifier of source
     * @return callback
     */
    auto make_callback(Key &&k) {
        std::lock_guard _(_mx);
        _count++;
        return [k = std::move(k), this](Value v) mutable {
            push({std::move(k),std::move(v)});
        };
    }

    ///pop ready keyvalue, if no values available, return empty object
    std::optional<kv_type> pop() const {
        std::lock_guard _(_mx);
        std::optional<kv_type> out;
        if (!_ready.empty()) {
            out = std::move(_ready.front());
            _ready.pop();
        }
        return out;        
    }
    
    struct awaiter {
        awaiter (aggregator &owner):_owner(owner) {}
        awaiter(const awaiter &) = default;
        awaiter &operator=(const awaiter &) = delete;
        
        bool await_ready() const {
            std::lock_guard _(_owner._mx);
            return !_owner._ready.empty();
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) const {
            std::lock_guard _(_owner._mx);            
            if (!_owner._ready.empty()) return h;
            _owner._suspended =h;
            return resume_lock::await_suspend(h, true);
        }
        
        kv_type await_resume() {
            std::lock_guard _(_owner._mx);
            kv_type out = std::move(_owner._ready.front());
            _owner._ready.pop();
            return out;
        }
        
        aggregator &_owner;
    };
    
    ///co_await
    awaiter operator co_await() {
        return *this;
    }
    
protected:
    mutable std::mutex _mx;
    std::queue<kv_type> _ready;
    std::size_t _count = 0;
    std::coroutine_handle<> _suspended;
    
};



}



#endif /* SRC_COCLASSES_AGGREGATOR_H_ */
