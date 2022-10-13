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

#include "abstract_awaiter.h"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <variant>
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
    
    thread_pool(unsigned int threads = 0)        
    {
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
            coroboard([&]{
                h->resume();
            });
            lk.lock();
        }
    }
    
    void stop() {
        std::vector<std::thread> tmp;
        std::queue<abstract_awaiter<> *> q;
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
            n->resume();
        }
    }

    ~thread_pool() {
        stop();
    }
    
    using awaiter = co_awaiter<thread_pool>;
    template<typename Fn>
    class fork_awaiter: public awaiter {
    public:
        fork_awaiter(thread_pool &pool, Fn &&fn):awaiter(pool), _fn(std::forward<Fn>(fn)) {}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
            Fn fn (std::forward<Fn>(_fn));
            std::coroutine_handle<> out = awaiter::await_suspend(h);            
            fn();
            return out;
        }
    protected:
        Fn _fn;
    };
    
    awaiter operator co_await() {
        return *this;
    }

    /*
     * BUG - GCC 10.3-12.2+ - do not inline lambda to this function
     * 
     * declare lambda as variable auto, and pass the variable to the
     * argument by std::move() - otherwise bad things can happen
     * 
     * https://godbolt.org/z/nz1coM5YP
     * 
     */ 
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
    std::queue<abstract_awaiter<> *> _queue;
    std::vector<std::thread> _threads;
    bool _exit = false;    
    static thread_pool * & current_pool() {
        static thread_local thread_pool *c = nullptr;
        return c;
    }
    
    friend class co_awaiter<thread_pool>;
    bool is_ready() noexcept {
        return _exit;
    }
    void get_result() {
        if (_exit) throw await_canceled_exception(); 
    }        
    bool subscribe_awaiter(abstract_awaiter<> *awt) noexcept {
        std::lock_guard lk(_mx);
        if (_exit) return false;
        _queue.push(awt);
        _cond.notify_one();
        return true;
    }



};


}



#endif /* SRC_COCLASSES_THREAD_POOL_H_ */
