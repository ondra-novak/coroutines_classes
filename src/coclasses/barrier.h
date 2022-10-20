/** @file barrier.h */

#pragma once
#ifndef SRC_COCLASSES_BARRIER_H_
#define SRC_COCLASSES_BARRIER_H_
#include "abstract_awaiter.h"

#include "common.h"
#include <atomic>
#include <cassert>

namespace cocls {

///Barier - block the coroutine until specifies count of coroutines arrives and is blocked
/**
 * You can set a number which defined when to barrier is automatically release. Or you
 * can release the barier manually
 */
class barrier {
public:
    
    ///Construct a barrier which can be released manually
    barrier():_count(-1) {}
    ///Construct a barrier which is automatically released when certain count of coroutines reaches
    barrier(unsigned int count):_count(count) {}
    
    
    using awaiter = co_awaiter<barrier,resumption_policy::unspecified<void>,true>;

    ///await the barrier
    awaiter operator co_await() {
        return *this;
    }
    
    ///release the barrier manually
    /** It is one hit action. if there is no awaiting coroutine, function does nothing */
    void release() {
        abstract_awaiter<true>::resume_chain(_waiting,nullptr);
    }
    
    
    
protected:
    
    friend class co_awaiter_base<barrier, true>;
    unsigned int _count;
    std::atomic<abstract_awaiter<true> *> _waiting = nullptr;
    
    bool is_ready() {
        //we always need to suspend the coroutine, to have chance to check resumption condition
        return false;
    }
    
    
    
    bool subscribe_awaiter(abstract_awaiter<true> *awt) {        
        awt->_next = nullptr;
        //first pick whole chain and put there nullptr - other threads will be paused too
        auto x = _waiting.exchange(nullptr, std::memory_order_relaxed);
        //count waiting threads
        unsigned int cnt = get_count(x);
        //count + 1 is still less then required
        if (cnt+1<_count) {
            //place me at the top
            awt->_next = x;
            //exchange back
            x = _waiting.exchange(awt, std::memory_order_relaxed);
            //there can be already awaitings
            while (x) {
                //process them one by one
                auto c = x;
                x = x->_next;
                //register them again - check for condition
                if (!subscribe_awaiter(c)) {
                    //last subscribe returned false
                    //the current queue, except c is already resumed
                    //so resume the c as well
                    c->resume(); 
                    
                }
            }
            //in this case, true continues in suspend - but it is not problem, whether the 
            //coroutine has been resumed, this is expected. 
            return true;
        } else {           
            //if the count has been met
            //resume whole chain (without me)
            abstract_awaiter<true>::resume_chain_lk(x, nullptr);
            //return false, so I can also continue
            return false;
        }                
    }
    
    void get_result() {
        //empty
    }
    

    unsigned int get_count(abstract_awaiter<true> *x) {
        //count waiting threads
        unsigned int cnt = 0;
        for (auto y = x; y; y = y->_next) {
            ++cnt;
        }
        return cnt;
    }
};


}



#endif /* SRC_COCLASSES_BARRIER_H_ */
