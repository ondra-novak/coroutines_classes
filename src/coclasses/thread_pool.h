/**
 * @file thread_pool.h
 *
 *  Created on: 6. 10. 2022
 *      Author: ondra
 */
#pragma once
#ifndef SRC_COCLASSES_THREAD_POOL_H_
#define SRC_COCLASSES_THREAD_POOL_H_
#include "common.h"
#include "future.h"
#include "exceptions.h"

#include "resumption_policy.h"
#include "lazy.h"
#include "function.h"

#include <condition_variable>
#include <functional>
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
 */

class thread_pool {
public:

    using q_item = function<void()>;

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
            auto h = std::move(_queue.front());
            _queue.pop();
            lk.unlock();
            resumption_policy::queued::install_queue_and_call(h);
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
        decltype(_threads) tmp;
        decltype(_queue) q;
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
    }

    ///Destroy the thread pool
    /**
     * It also stops all threads
     */
    ~thread_pool() {
        stop();
    }


    class co_awaiter {
    public:
        co_awaiter() = default;
        co_awaiter(thread_pool &owner):_owner(&owner) {}
        co_awaiter(const co_awaiter&) = default;
        co_awaiter &operator=(const co_awaiter&) = delete;

        class resume_ntf_cancel {
        public:
            resume_ntf_cancel(std::coroutine_handle<> h, co_awaiter *ntf):_h(h),_ntf(ntf) {}
            resume_ntf_cancel(resume_ntf_cancel &&other):_h(other._h),_ntf(other._ntf) {
                other._ntf= nullptr;
            }
            void operator()() {
                _ntf = nullptr;
                _h.resume();
            }
            ~resume_ntf_cancel() {
                if (_ntf) {
                    _ntf->_canceled = true;
                    _h.resume();
                }
            }
        protected:
            std::coroutine_handle<> _h;
            co_awaiter *_ntf;
        };

        static constexpr bool await_ready() {return false;}

        void await_suspend(std::coroutine_handle<> h) {
            _owner->enqueue(resume_ntf_cancel(h, this));
        }

        void await_resume() {
            if (_canceled) throw await_canceled_exception();
        }

    protected:
        thread_pool *_owner = nullptr;
        bool _canceled = false;
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
    co_awaiter operator co_await() {
        return *this;
    }

    ///Run function in thread pool
    /**
     *
     * @param fn function to run. The function must return void. The function
     * run() returns immediately
     */
    template<typename Fn>
    CXX20_REQUIRES(std::same_as<void, decltype(std::declval<Fn>()())>)
    void run_detached(Fn &&fn) {
        enqueue(q_item(std::forward<Fn>(fn)));
    }

    ///Runs function in thread pool, returns future
    /**
     * Works similar as std::async. It just runs function in thread pool and returns
     * cocls::future.
     * @param fn function to run
     * @return future<Ret> where Ret is return value of the function
     */
    template<typename Fn>
    auto run(Fn &&fn) -> future<decltype(std::declval<Fn>()())> {
        using RetVal = decltype(std::declval<Fn>()());
        return [&](auto promise) {
            run_detached([fn = std::forward<Fn>(fn), promise = std::move(promise)]() mutable {
                try {
                    if constexpr(std::is_void_v<RetVal>) {
                        fn();
                        promise();
                    } else {
                        promise(fn());
                    }
                } catch(...) {
                    promise(std::current_exception());
                }
            });
        };
    }
    ///Resolve promise in thread
    /** Allocates thread and resolves promise in it. If the
     * associated coroutine is resumed, it is resumed in this allocated thread
     * @param t promise
     * @param args arguments
     *
     * @note if the promise t is not valid, no thread is allocated
     */
    template<typename T, typename ... Args>
    void resolve(promise<T> &t, Args && ... args) {
        if (t) run_detached(t.bind(std::forward<Args>(args)...));
    }

private:

    template<typename T, typename P>
    class async_ext: public async<T,P> {
    public:
        typename async<T,P>::promise_type &get_promise() {return this->_h.promise();}
        using async<T,P>::resume_by_policy;
    };

public:

    ///Run coroutine async
    /**
     * @param coro coroutine to run
     * @param args optional arguments passed to resumption policy.
     * @return future which resolves when coroutine ends
     */
    template<typename T, typename P, typename ... Args>
    future<T> run(async<T,P> &&coro, Args && ... args) {
        return [&](auto promise) {
            auto &ex = static_cast<async_ext<T,P> & >(coro);;
            typename async<T,P>::promise_type &p = ex.get_promise();
            p.initialize_policy(std::forward<Args>(args)...);
            p._future = promise.claim();
            run_detached([c = std::move(ex)]() mutable {
                c.resume_by_policy();
            });
        };
    }

    ///Runs coroutine async, discard future
    /**
     * @param coro coroutine to run
     * @param args optional arguments passed to resumption policy.
     */
    template<typename T, typename P, typename ... Args>
    void run_detached(async<T,P> &&coro, Args && ... args) {
        auto &ex = static_cast<async_ext<T,P> & >(coro);;
        typename async<T,P>::promise_type &p = ex.get_promise();
        p.initialize_policy(std::forward<Args>(args)...);
        run_detached([c = std::move(ex)]{
            c.resume_by_policy();
        });
    }



    struct current {

        class  current_awaiter: public co_awaiter {
        public:
            current_awaiter():co_awaiter(*_current) {}
            static bool await_ready() {
                thread_pool *c = _current;
                return c == nullptr || c->_exit;
            }
        };

        current_awaiter operator co_await() {
            return current_awaiter();
        }

        static bool is_stopped() {
            thread_pool *c = _current;
            return !c || c->is_stopped();
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


    ///returns true if there is still enqueued task
    bool any_enqueued() {
        std::unique_lock lk(_mx);
        return _exit || !_queue.empty();
    }

    friend bool is_current(const thread_pool &pool) {
        return _current == &pool;
    }





protected:


    void enqueue(q_item &&fn) {
        std::lock_guard _(_mx);
        if (!_exit) {
            _queue.push(std::move(fn));
            _cond.notify_one();
        }
    }


    mutable std::mutex _mx;
    std::condition_variable _cond;
    std::queue<q_item> _queue;
    std::vector<std::thread> _threads;
    bool _exit = false;
    static thread_local thread_pool *_current;




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


    bool is_policy_ready() const noexcept {
        return _cur_pool != nullptr;
    }


    thread_pool() = default;
    thread_pool(const shared_thread_pool &pool):_cur_pool(pool) {}

    shared_thread_pool _cur_pool = nullptr;
    using initial_awaiter = initial_resume_by_policy<thread_pool>;

    void resume(std::coroutine_handle<> h) {
        _cur_pool->run_detached([=]{
            h.resume();
        });
    }

    std::coroutine_handle<> resume_handle(std::coroutine_handle<> h) noexcept {
        if (is_current(*_cur_pool)) return h;
        _cur_pool->run_detached([=]{
            h.resume();
        });
        return std::noop_coroutine();
    }

    ///Initializes policy
    /**
     * @param pool shared thread pool
     * @retval true you need to resume coroutine
     * @retval false you don't need to resume coroutine
     *
     */
    bool initialize_policy(shared_thread_pool pool) {
        bool ret = _cur_pool == nullptr;
        _cur_pool = pool;
        return ret;

    }
    std::coroutine_handle<> resume_handle_next() noexcept {
        return resumption_policy::queued::resume_handle_next();
    }
    static bool can_block() {
        return !cocls::thread_pool::current::any_enqueued();
    }


};
}


}



#endif /* SRC_COCLASSES_THREAD_POOL_H_ */
