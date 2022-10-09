/*
 * thread_pool.h
 *
 *  Created on: 6. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_THREAD_POOL_H_
#define SRC_COCLASSES_THREAD_POOL_H_
#include "common.h"
#include "resume_lock.h"
#include "exceptions.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace cocls {

///thread pool for coroutines. 
/** Main benefit of such object is zero allocation during transfering the coroutine to the
 * other thread
 * 
 * Each thread also initializes the coboard() so coroutines can be scheduled manually inside of
 * each thread
 * 
 */

class thread_pool {
public:
    
    thread_pool(unsigned int threads = 0) {
        if (!threads) threads = std::thread::hardware_concurrency();
        for (unsigned int i = 0; i < threads; i++) {
            _threads.push_back(std::thread([this]{worker();}));
        }
    }
    
    void worker() {
        current_pool() = this;
        std::unique_lock lk(_mx);
        for(;;) {
            _cond.wait(lk, [&]{return !_queue.empty() || _exit;});
            if (_exit) break;
            auto h = _queue.front();
            _queue.pop();
            lk.unlock();
            resume_lock::resume(h);
            lk.lock();
        }
    }
    
    void stop() {
        std::vector<std::thread> tmp;
        std::queue<std::coroutine_handle<> > q;
        {
            std::unique_lock lk(_mx);
            _exit = true;
            _cond.notify_all();
            std::swap(tmp, _threads);
            std::swap(q, _queue);
        }
        for (auto &t: tmp) t.join();
        while (!q.empty()) {
            auto n = std::move(q.front());
            q.pop();
            n.resume();
        }
    }

    ~thread_pool() {
        stop();
    }

    class awaiter_base {
    public:
        awaiter_base(thread_pool &owner):_owner(owner) {}
        awaiter_base(const awaiter_base &owner) = default;
        awaiter_base &operator=(const awaiter_base &owner) = delete;
    protected:
        thread_pool &_owner;

    };
    
    class awaiter: public awaiter_base { 
    public:
        using awaiter_base::awaiter_base;
        
        bool await_ready() noexcept {
            return _owner._exit;
        }
        void await_resume() {
            if (_owner._exit) throw await_canceled_exception(); 
        }        
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
            std::lock_guard lk(_owner._mx);
            if (_owner._exit) return h;
            _owner._queue.push(h);
            _owner._cond.notify_one();
            return resume_lock::await_suspend();
        }
    };
    
    template<typename Fn>
    class fork_awaiter: public awaiter {
    public:
        fork_awaiter(thread_pool &pool, Fn &&fn):awaiter(pool), _fn(std::forward<Fn>(fn)) {}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
            std::coroutine_handle<> out = awaiter::await_suspend(h);            
            _fn();
            return out;
        }
    protected:
        Fn _fn;
    };
    
    awaiter operator co_await() {
        return *this;
    }

    template<typename Fn>
    fork_awaiter<Fn> fork(Fn &&fn) {
        return fork_awaiter<Fn>(*this, std::forward<Fn>(fn));
    }
    
    struct current {
        
        class  current_awaiter: public awaiter {
        public:
            current_awaiter():awaiter(*current_pool()) {}
            static bool await_ready() {
                thread_pool *c = current_pool();
                return c == nullptr || c->_exit;
            }
        };
        
        template<typename Fn>
        class current_fork_awaiter: public fork_awaiter<Fn> {
        public:
            current_fork_awaiter(Fn &&fn):fork_awaiter<Fn>(*current_pool(), std::forward<Fn>(fn)) {}
            static bool await_ready() {
                thread_pool *c = current_pool();
                return c == nullptr || c->_exit;
            }            
        };


        current_awaiter operator co_await() {
            return current_awaiter();
        }
        template<typename Fn>
        static current_fork_awaiter<Fn> fork(Fn &&fn) {
            return current_fork_awaiter<Fn>(std::forward<Fn>(fn));
        }
        static bool is_stopped() {
            thread_pool *c = current_pool();
            return !c || c->is_stopped();
        }

    };
    
    bool is_stopped() const {
        std::lock_guard _(_mx);
        return _exit;
    }
    
protected:
    mutable std::mutex _mx;
    std::condition_variable _cond;
    std::queue<std::coroutine_handle<>> _queue;
    std::vector<std::thread> _threads;
    bool _exit = false;
    static thread_pool * & current_pool() {
        static thread_local thread_pool *c = nullptr;
        return c;
    }
};


}



#endif /* SRC_COCLASSES_THREAD_POOL_H_ */
