/**
 * @file resume_lock.h
 */
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



    ///Handle suspension of coroutine through the resume lock
    /**
     * It is expected, that function is called from the await_suspend, which
     * returns std::coroutine_handle<>. (it is recommended to have all these
     * function returning std::coroutine_handle<>. Returning void or bool is 
     * not much useful)
     * 
     * @return function returns handle of coroutine ready to be resume, which
     * can be unrelated to suspending coroutine. Because current coroutine is
     * being suspended, the resume_lock object can choose which coroutine will
     * be resumed for next run. If there is no coroutine ready, function returns 
     * std::noop_coroutine(), which eventually returns execution to the
     * original caller. 
     * 
     *
     * @code 
     *  std::coroutine_handle await_suspend(std::coroutine_handle h) {
     *          _awaiting = h;
     *          return resume_lock::await_suspend();
     * }
     * @endcode
     */
    static std::coroutine_handle<> await_suspend() {
        return get_instance().await_suspend_impl();
    }



    
    ///Resume coroutine
    /**
     * @param h handle of coroutine to resume
     * 
     * @note this function can return immediately, when resume_lock is active, however
     * the coroutine specified by the argument is scheduled to resume on nearest
     * await_suspend(). If there are more such coroutines, they are managed in
     * a queue.
     */
    static void resume(const std::coroutine_handle<> & h) {
        get_instance().resume_impl(h);
    }

    ///Run coroutine board
    /**
     * Coroutine board is base level for coroutines. It allows to schedule multiple 
     * coroutines where one awaits to other. The feature is build on top of resume_lock. 
     * When coroboard() is started, resume lock is held and then specified function is called. 
     * Once the function returns, the coroboard() processes all scheduled coroutines. During
     * this phase, queue can be also extended. 
     * 
     * Function exits, when queue is empty. This can happen, when all coroutines exits or
     * when they is transfered to different thread.
     * 
     * Main purpose if this function is to avoid starvation of a coroutine which is started
     * as first, because it can be probably suspended for long time while waiting to
     * clean up the resume queue. By starting coroboard() transfers this issue to the coroboard() itself
     * where it is harmless
     * 
     * @param fn function to call on top of coroutine board
     */
    template<typename Fn>
    static void coroboard(Fn &&fn) {
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
            return lk.await_suspend_impl();
        }        
    };
    
    struct pause_suspender {
        pause_awaiter operator co_await() const {return {};}
    };
    
    
    ///Suspend current coroutine and reschedules it to the final phase of resume_lock / coroboard()
    /**
     * To use this function you need to call `co_await pause()`
     * 
     * The main purpose of this function to move execution of the corutine out of current stack.
     * If there is coroboard() or resume lock active, the execution is scheduled to its final
     * phase. 
     * 
     * You can use this function to manually schedule multiple coroutines. If all coroutines
     * use this function periodically the execution becomes interleaved as each use of pause()
     * moves this coroutine to the queue and removes other coroutine from the queue and resumes
     * it.
     * 
     * If there is no active coroboard(), or no active resume lock the current coroutine isn't suspended 
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

    std::coroutine_handle<> await_suspend_impl() {
        if (_active && !_waiting.empty()) {
            auto h = _waiting.front();
            _waiting.pop();
            return h;
        } else {
            return std::noop_coroutine();
        }            
    }



    template<typename Fn> 
    void lock(Fn &&fn) noexcept {
        bool prev_active = _active;
        _active = true;
        fn();
        _finish = true;
        while (!_waiting.empty()) {
            auto h = _waiting.front();
            _waiting.pop();
            h.resume();
        }
        _active = prev_active;
        _finish = false;
        
    }
    
    void resume_impl(const std::coroutine_handle<> & h) {
        if (_active) {
            _waiting.push(h);
            return;
        }
        lock([&]{
            h.resume();
        });
    }     
protected:
    bool _active = false;
    bool _finish = false;
    std::queue<std::coroutine_handle<> > _waiting;
   
};


///Run coroutine board
/**
 * @copydoc resume_lock::coroboard()
 */
template<typename Fn>
inline void coroboard(Fn &&fn) {
    resume_lock::coroboard(std::forward<Fn>(fn));
}

///Suspend current coroutine and reschedules it to the final phase of resume_lock / coroboard()
/**
 * @copydoc resume_lock::pause
 */
inline resume_lock::pause_suspender pause() {
    return resume_lock::pause();
}


template<typename Fn>
class resume_me_awaiter {
public:
    resume_me_awaiter(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
    static bool await_ready() noexcept {return false;}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        if constexpr(std::is_convertible_v<decltype(_fn(h)), std::coroutine_handle<> >) {
            return _fn(h);
        } else {
            _fn(h);
            return resume_lock::await_suspend();
        }
    }
    static void await_resume() {}
    
protected:
    Fn _fn;
};


///Suspend current coroutine and forward its handle to somewhere
/**
 * @code
 * co_await suspend([](std::coroutine_handle<>){...})
 * @endcode
 * 
 * Function suspend current coroutine in exchange to retrieve its handle and calls
 * a function with that handle. It can be useful to register this coroutine for a signal
 * @param fn function to be called with handle
 
 * @return awaiter
 */
template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<std::coroutine_handle<> >()))>
resume_me_awaiter<Fn> suspend(Fn &&fn) {
    return resume_me_awaiter<Fn>(std::forward<Fn>(fn)); 
}


}



#endif /* SRC_COCLASSES_RESUME_LOCK_H_ */
