/**
 * @file thread_pool.h
 *
 *  Created on: 6. 10. 2022
 *      Author: ondra
 */
#pragma once
#ifndef SRC_COCLASSES_THREAD_POOL_H_
#define SRC_COCLASSES_THREAD_POOL_H_
#include "awaiter.h"
#include "common.h"
#include "exceptions.h"

#include "resumption_policy.h"
#include "lazy.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <variant>
#include <vector>

namespace cocls {

///thread pool for coroutines. 
/** Main benefit of such object is zero allocation during transferring the coroutine to the
 * other thread
 * 
 * Each thread also initializes the coroboard() so coroutines can be scheduled manually inside of
 * each thread
 * 
 */

class thread_pool {
public:
    
    ///Start thread pool
    /**     
     * @param threads count of threads. Default value creates same amount as count 
     * of available CPU cores (hardware_concurrency)
     */
    thread_pool(unsigned int threads = 0)        
    {
        if (!threads) threads = std::thread::hardware_concurrency();
        for (unsigned int i = 0; i < threads; i++) {
            _threads.push_back(std::thread([this]{worker();}));
        }
    }

    
    ///Start a worker
    /**
     * By default, workers are started during construction. This function allows
     * to add a worker. Current thread becomes a worker until stop() is called.
     */
    void worker() {
        _current = this;
        std::unique_lock lk(_mx);
        for(;;) {
            _cond.wait(lk, [&]{return !_queue.empty() || _exit;});
            if (_exit) break;
            auto h = _queue.front();
            _queue.pop();
            lk.unlock();
            resumption_policy::queued::resume(h->resume_handle());
            //if _current is nullptr, thread_pool has been destroyed 
            if (_current == nullptr) return;
            lk.lock();
        }
    }
    
    ///Stops all threads
    /**
     * Stopped threads cannot be restarted
     */
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
        auto me = std::this_thread::get_id();
        for (std::thread &t: tmp) {
            if (t.get_id() == me) {
                t.detach();
                //mark this thread as ordinary thread
                _current = nullptr;
            }
            else {
                t.join();
            }
        }
        while (!q.empty()) {
            auto n = std::move(q.front());
            q.pop();
            n->resume();
        }
    }

    ///Destroy the thread pool 
    /**
     * It also stops all threads
     */
    ~thread_pool() {
        stop();
    }
    
    class awaiter : public co_awaiter<thread_pool> {
    public:
        using co_awaiter<thread_pool>::co_awaiter;

    private:
        using co_awaiter<thread_pool>::set_resumption_policy;
        
    };
    template<typename Fn>
    class fork_awaiter: public awaiter {
    public:
        fork_awaiter(thread_pool &pool, Fn &&fn):awaiter(pool), _fn(std::forward<Fn>(fn)) {}
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            Fn fn (std::forward<Fn>(_fn));
            bool b = awaiter::await_suspend(h);            
            fn();
            return b;
        }
    protected:
        Fn _fn;
    };
    
    
    ///Transfer coroutine to the thread pool
    /**
     * 
     * @return awaiter
     * 
     * @code
     * task<> coro_test(thread_pool &p) {
     *      co_await p;
     *      //now we are running in the thread pool
     * }
     * @endcode
     */
    awaiter operator co_await() {
        return *this;
    }
    
    ///start a anonymous awaiter - called from thread_pool_resumption_policy 
    /**
     * @param aw awaiter to start in thread pool
     */
    void enqueue(abstract_awaiter<> *aw) {
        bool ok = subscribe_awaiter(aw);
        if (!ok) {
            aw->resume();
        }        
    }

    ///start a lazy task in the thread pool
    /**
     * @param t task to start
     * @retval true started
     * @retval false can't be started, already running or already scheduled
     */
    template<typename T, typename _P>
    bool enqueue(lazy<T,_P> t) {
        
        class awaiter: public abstract_owned_awaiter<thread_pool> {
        public:
            awaiter(thread_pool &owner, lazy<T> t, std::coroutine_handle<> h)
                :abstract_owned_awaiter<thread_pool>(owner)
                ,_t(t), _h(h) {}
            void resume_canceled() noexcept{
                _t.mark_canceled();
                _policy.resume(_h);
                delete this;            
            }

            virtual void resume() noexcept override {
                if (_owner.is_stopped()) _t.mark_canceled();
                _policy.resume(_h);
                delete this;
            }
            virtual std::coroutine_handle<> resume_handle() noexcept override {
                if (_owner.is_stopped()) _t.mark_canceled();
                auto out = _h;
                delete this;
                return out;
            }
        protected:
            lazy<T> _t;
            std::coroutine_handle<> _h;
            _P _policy;
            
        };
        
        std::coroutine_handle<> h = t.get_start_handle();
        if (h == nullptr) return false;
        awaiter *aw = new awaiter(*this, t, h);
        bool ok = subscribe_awaiter(aw);
        if (!ok) {
            aw->resume_canceled();
        }
        return true;
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
    /// For the code, transfers coroutine to different thread while some code continues in this thread
    /**
     * @param fn function to be called in current thread after coroutine is transfered.
     * @return awaiter you need to await for the result to execute this fork
     * 
     * @code
     * co_await pool.fork([=]{
     *          //forked code
     * })
     * @endcode
     * 
     * @note BUG - GCC 10.3-12.2+ - do not inline lambda to this function: https://godbolt.org/z/nz1coM5YP
     * 
     * @code
     * auto forked = [=] {
     *          //forked code
     * };
     * co_await pool.fork(std:::move(forked));
     * @endcode
     * 
     * 
     */
 
    template<typename Fn>
    fork_awaiter<Fn> fork(Fn &&fn) {
        return fork_awaiter<Fn>(*this, std::forward<Fn>(fn));
    }
    
    struct current {
        
        class  current_awaiter: public awaiter {
        public:
            current_awaiter():awaiter(*_current) {}
            static bool await_ready() {
                thread_pool *c = _current;
                return c == nullptr || c->_exit;
            }
        };
        
        template<typename Fn>
        class current_fork_awaiter: public fork_awaiter<Fn> {
        public:
            current_fork_awaiter(Fn &&fn):fork_awaiter<Fn>(*_current, std::forward<Fn>(fn)) {}
            static bool await_ready() {
                thread_pool *c = _current;
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
            thread_pool *c = _current;
            return !c || c->is_stopped();
        }
        
        ///run enqueued task
        /**
         * @retval true a task has been run
         * @retval false no task was in queue
         */
        static bool give_way() {
            thread_pool *c = _current;
            if (c) return c->give_way();
            return false;
        }
        ///returns true if there is still enqueued task
        static bool any_enqueued() {
            thread_pool *c = _current;
            if (c) return c->any_enqueued();
            return false;
            
        }

    };
    
    bool is_stopped() const {
        std::lock_guard _(_mx);
        return _exit;
    }

    
    bool give_way() {
        std::unique_lock lk(_mx);
        if (!_exit && !_queue.empty()) {
            auto h = _queue.front();
            _queue.pop();
            lk.unlock();
            resumption_policy::queued::resume(h->resume_handle());
            return true;
        }       
        return false;
    }
    
    ///returns true if there is still enqueued task
    bool any_enqueued() {
        std::unique_lock lk(_mx);
        return _exit || !_queue.empty();
    }

protected:
    mutable std::mutex _mx;
    std::condition_variable _cond;
    std::queue<abstract_awaiter<> *> _queue;
    std::vector<std::thread> _threads;
    bool _exit = false;    
    static thread_local thread_pool *_current;
    
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

inline thread_local thread_pool *thread_pool::_current = nullptr;

using shared_thread_pool = std::shared_ptr<thread_pool>;

namespace resumption_policy {



///Thread pool policy need shared thread pool (std::shared_ptr<thread_pool>)
/**
 * Because the task cannot initialize its resumption policy, it is not started until
 * the policy is initialized. Use task<>::initialize_policy(shared_ptr<thread_pool>).
 * Once the policy is initialized, the task is started
 *  
 * 
 */
struct thread_pool {
    
    thread_pool() = default;
    
    using initial_awaiter = initial_resume_by_policy<thread_pool>;
    
    class resumer: public abstract_awaiter<> {
    public:
        shared_thread_pool _cur_pool = nullptr;
        std::coroutine_handle<> _h;
        virtual void resume() noexcept override{
            queued::resume(_h);
        }
        void set_handle(std::coroutine_handle<> h) {
            _h = h;
            if (_cur_pool != nullptr) _cur_pool->enqueue(this);
        }
        void set_pool(shared_thread_pool pool) {
            bool start = _cur_pool == nullptr;
            _cur_pool = pool;
            if (start) [[likely]] {
                _cur_pool->enqueue(this);
            }
        }
    };
    
    resumer _resumer;
    
        
    void resume(std::coroutine_handle<> h) {
        _resumer.set_handle(h);
    }
    ///Initializes policy
    /**
     * @param pool shared thread pool
     * 
     * called from task<>::initialize_policy();
     * 
     * 
     */
    void initialize_policy(shared_thread_pool pool) {
        _resumer.set_pool(pool);
    }
    
    thread_pool(const shared_thread_pool &pool) {
        initialize_policy(pool);
    }
};
}

}



#endif /* SRC_COCLASSES_THREAD_POOL_H_ */
