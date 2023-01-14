#pragma once
#ifndef SRC_COCLASSES_SRC_COCLASSES_COUNTER_H_
#define SRC_COCLASSES_SRC_COCLASSES_COUNTER_H_
#include "awaiter.h"
#include <mutex>


namespace cocls {


///Counter is coroutine synchronizing primitive
/**
 * Counter counts up or down. If the value is positive, any awaiting
 * coroutine is suspended. Once counter reaches zero, or below zero, awaiting
 * coroutines are resumed. When counter is zero or below zero, co_await 
 * is immediately finished with no suspension
 * 
 * Counter is MT safe.  Counter can be directly co_awaited. It returns no value
 * when it is resumed.
 */
class Counter {
public:
    using awaiter = cocls::abstract_awaiter<true>;

    ///Initialize counter to zero
    Counter():_count(0) {}
    ///Initialize counter to exact value
    explicit Counter(long count):_count(count) {}
    ///Initialize counter from other counter
    /**
     * @param other other counter - note only value is copied.
     * 
     */
    Counter(const Counter &other):_count(other.get_value()) {}

    ///Assign to the counter
    /**
     * @param other other counter
     * @return this
     * 
     * @note equivalent operation to set_value(other.get_value())
     */ 
    Counter &operator=(const Counter &other) {
        if (this != &other) {
            set_value(other.get_value());
        }
        return *this;
    }
    
    ///Coawait counter
    co_awaiter<Counter, true> operator co_await() {
        return *this;
    }
    
    ///increment value
    long increment() {
        return ++_count;
    }
    
    long operator++() {
        return increment();
    }
    long operator++(int) {
        return increment()-1;
    }

    
    ///decrement value
    long decrement() {
        long r = --_count; 
        if (r == 0) {
            awaiter::resume_chain(_chain, nullptr);
        }
        return r;
    }
    
    long operator--() {
        return decrement();
    }
    long operator--(int) {
        return decrement()+1;
    }

    
    ///set value
    /**
     * @param val new value. 
     * 
     * @note by setting under 1 releases any awaiting coroutines
     */
    void set_value(long val) {
        long diff = val - _count.load();
        if (_count.fetch_add(diff) + diff <= 0) {
            awaiter::resume_chain(_chain, nullptr);
        }
    }
    
    ///retrieve current values
    long get_value() const {
        return _count;
    }
    
    
protected:
    std::atomic<awaiter *> _chain = nullptr;
    std::atomic<long> _count;
    
    
    friend class co_awaiter<Counter, true>;
    
    bool is_ready() const {
        return _count <= 0;
    }
    
    bool subscribe_awaiter(awaiter *awt) noexcept {
        awt->subscibre_check_ready(_chain);
        if (_count <= 0) {
            awaiter::resume_chain(_chain, awt);
            return false;
        }        
        return true;
    }
    
    void get_result() noexcept {}; 

};


}




#endif /* SRC_COCLASSES_SRC_COCLASSES_COUNTER_H_ */
