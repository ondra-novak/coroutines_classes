#pragma once
#ifndef SRC_COCLASSES_RESUME_LOCK_H_
#define SRC_COCLASSES_RESUME_LOCK_H_

#include "common.h"

#include <coroutine>
#include <queue>

namespace cocls {


///Resume lock - controls resumptions of coroutines
/**
 * A main idea behind of this class is to avoid resume-recursion. Instead of the resumption, 
 * resumption requests are placed into a queue and resumed during nearest suspension point
 * 
 * The very first resume is processed normally, but a thread-local lock is held. Additional
 * resume requests are placed into queue. In case of suspension point, first waiting coroutine
 * is removed from the queue and scheduler (using symmetric transfer)
 * 
 * To use resume_lock_t, you need to call two special functions of this object instead default
 * ones.
 * 
 * In an awaiter, for implementation of await_suspend() function, use resume_lock::await_suspend 
 * and return its result as the result if above function.
 * 
 * Instead std::coroutine_handle<>::resume(), call resume_lock::resume()
 * 
 * 
 */
class resume_lock {
public:

    ///Handle suspension of coroutine through the resume_lock
    /**
     * 
     * Function schedules next waiting coroutine if there is such. Otherwise, schedules 
     * return to the original caller
     * 
     * @param h handle of coroutine being suspended.
     * @param suspend result to be returned by standard await_suspend. This is for convince. If
     * this argument is true, the coroutine will be suspended. if this argument is false, 
     * the coroutine will not be suspended.
     * @return handle of coroutine scheduled to resume. Just return this value as result
     * of awaiter's function await_suspend();
     */
    static std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, bool suspend = true) {
        return get_instance().await_suspend_impl(h, suspend);
    }
    
    ///Resume coroutine
    /**
     * @param h handle of coroutine to resume
     * 
     * @note this function can return immediately, when resume_lock is active, however
     * the coroutine specified by the argument is scheduled.
     */
    static void resume(std::coroutine_handle<> h) {
        get_instance().resume_impl(h);
    }

    ///Run coroutine board
    /**
     * Coroutine board is base level for coroutines. It allows to schedule multiple 
     * coroutines where one awaits to other. The feature is build on top of resume_lock. 
     * When coboard is started, resume lock is held and then specified function is called. 
     * Once the function returns, the coboard processes all scheduled coroutines. During
     * this phase, queue can be also extended. 
     * 
     * Function exits, when queue is empty. This can happen, when all coroutines exits or
     * when they is transfered to different thread.
     * 
     * Main purpose if this function is to avoid starvation of a coroutine which is started
     * as first, because it can be probably suspended for long time while waiting to
     * clean up the resume queue. By starting coboard transfers this issue to the coboard itself
     * where it is harmless
     * 
     * @param fn function to call on top of coroutine board
     */
    template<typename Fn>
    static void coboard(Fn &&fn) {
        get_instance().lock(std::forward<Fn>(fn));
    }
    
    struct pause_awaiter: std::suspend_always {
        static bool await_ready() noexcept {
            resume_lock &lk = get_instance();
            return !lk._active;
        }
        static std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {                        
            resume_lock &lk = get_instance();
            lk._waiting.push(h);
            if (!lk._finish) return std::noop_coroutine();
            return lk.await_suspend_impl(h, true);
        }        
    };
    
    struct pause_suspender {
        pause_awaiter operator co_await() const {return {};}
    };
    
    
    ///Suspend current coroutine and reschedules it to the final phase of resume_lock / coboard
    /**
     * To use this function you need to call `co_await pause()`
     * 
     * The main purpose of this function to move execution of the corutine out of current stack.
     * If there is coboard or resume lock active, the execution is scheduled to its final
     * phase. 
     * 
     * You can use this function to manually schedule multiple coroutines. If all coroutines
     * use this function periodically the execution becomes interleaved as each use of pause()
     * moves this coroutine to the queue and removes other coroutine from the queue and resumes
     * it.
     * 
     * If there is no active coboard, or no active resume lock the current coroutine isn't suspended 
     * and continues normally.  
     * 
     * @return object which can be co_awated
     */
    static pause_suspender pause() {return {};}
        
    
protected:
    static resume_lock &get_instance() {
        static thread_local resume_lock l;
        return l;
    }

    
    std::coroutine_handle<> await_suspend_impl(std::coroutine_handle<> h, bool suspend = true) {
        if (suspend) {
            if (_active && !_waiting.empty()) {
                auto h = _waiting.front();
                _waiting.pop();
                return h;
            } else {
                return std::noop_coroutine();
            }            
        } else {
            return h;
        }
    }
    
    template<typename Fn> 
    void lock(Fn &&fn) noexcept {
        if (_active) {
            //when coboard is active, just run function
            fn();
            return;
        }
        _active = true;
        fn();
        _finish = true;
        while (!_waiting.empty()) {
            auto h = _waiting.front();
            _waiting.pop();
            h.resume();
        }
        _active = false;
        _finish = false;
        
    }
    
    void resume_impl(std::coroutine_handle<> h) {
        if (_active) {
            _waiting.push(h);
        } else {
            lock([&]{
                h.resume();
            });
        }
    }     
protected:
    bool _active = false;
    bool _finish = false;
    std::queue<std::coroutine_handle<> > _waiting;
};


///Run coroutine board
/**
 * @copydoc resume_lock::coboard
 */
template<typename Fn>
inline void coboard(Fn &&fn) {
    resume_lock::coboard(std::forward<Fn>(fn));
}

///Suspend current coroutine and reschedules it to the final phase of resume_lock / coboard
/**
 * @copydoc resume_lock::pause
 */
inline resume_lock::pause_suspender pause() {
    return resume_lock::pause();
}

}



#endif /* SRC_COCLASSES_RESUME_LOCK_H_ */
