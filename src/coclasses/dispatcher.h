/** @file dispatcher.h
 *
 */
#pragma once
#ifndef SRC_COCLASSES_DISPATCHER_H_
#define SRC_COCLASSES_DISPATCHER_H_
#include "awaiter.h"
#include "exceptions.h"
#include "future.h"
#include "priority_queue.h"


#include <memory>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <type_traits>

namespace cocls {

namespace resumption_policy {
    struct dispatcher;
}
///Exception:
/**
 * Thrown when you call dispatcher::await before dispatcher is initialized
 */
class no_thread_dispatcher_is_initialized_exception: public std::exception {
public:
    const char *what() const noexcept override {
        return "No thread's dispatcher is initialized on the current thread";
    }
};


///Exception:
/**
 * Thrown, when coroutine is being suspended on dispatcher's thread, which is no longer exists
 * (has already ended)
 */
class home_thread_already_ended_exception: public await_canceled_exception {
public:
    const char *what() const noexcept override {
        return "Can't resume coroutine, the home thread has already ended";
    }

};


///assign the current thread a dispatcher, which allows to dispatch coroutines
/**
 * Dispatcher allows to accept and schedule coroutines on the thread for which the dispatcher
 * was created. Without it, the resumption_policy::dispatcher will not able to resume
 * coroutine on the current thread.
 *
 * To initialize dispatcher, you need to explicitly call thread_dispatcher::init()
 *
 * The coroutines are not resumed until the dispatcher is given chance to run. You need to
 * call thread_dispatcher::await() which suspends current function and allows
 * to other coroutines to be executed. function exits, when the awaiter passed to the
 * function becomes signaled.
 */

class dispatcher {
public:


    ///Initialized dispatcher in current thread
    /**
     * Futher calling this function doesn't nothing. You can't deinitialize the dispatcher
     */
    static void init() {
        if (instance != nullptr) [[unlikely]] return;
        instance = std::make_shared<dispatcher>();
    }

    ///awaits on an awaiter
    /**
     * Runs dispatcher until specified awaiter becomes signaled
     *
     * This is equivalent to
     * @code
     * auto res =co_await awt;
     * @endcode
     * ... but in normal routine. The coroutine should always use co_await. The
     * function blocks current execution and starts to process enqueued coroutines. When
     * all enqueued coroutines are processed, it starts to wait for newly enqueued coroutines
     * until the awaiter becomes signaled. Then exits and returns value.
     *
     * @param awt an expression which results to an awaiter to be awaiter
     * @return return value of the expression
     * @exception no_thread_dispatcher_is_initialized_exception you must explicitly call
     *   dispatcher::init();
     *
     * @note it is possible to call function recursively - meaning that inside of running
     * coroutine, you can call await() to interrupt the current coroutine. However
     * use it only if you have no other options. It is better to co_await on
     * the awaiter.
     */

    template<typename Awt>
    static auto await(Awt &&awt) {
        class Listener: public abstract_listening_awaiter<Awt &> {
        public:
            bool exit_flag = false;
            std::shared_ptr<dispatcher> owner;
            virtual void resume() noexcept override {
                owner->quit(exit_flag);
            }
        };
        if (instance == nullptr) throw no_thread_dispatcher_is_initialized_exception();
        Listener lsn;
        lsn.owner = instance;
        lsn.await([&]()->Awt &{return awt;});
        instance->run(lsn.exit_flag);
        if constexpr(!std::is_void_v<decltype(lsn.value())>) {
            return std::move(lsn.value());
        }
    }
    ///Pauses current execution and gives chance to enqueued coroutines to run
    /**
     * equivalent code
     * @code
     * co_await cocls::pause<>()
     * @endcode
     *
     * You can use this in normal routine if it is running on dispatcher's thread.
     * The coroutine should always use co_await cocls::pause<>()
     */
    static void pause() {
        if (instance == nullptr) throw no_thread_dispatcher_is_initialized_exception();
        instance->flush_queue();
    }

    ///schedule coroutine to run in the dispatcher's thread
    /**
     * @param h coroutine handle
     */
    void schedule(std::coroutine_handle<> h) {
        std::lock_guard lk(_mx);;
        _queue.push(h);
        _cond.notify_one();
    }
    ///schedule coroutine to run in the dispatcher's thread scheduled at given timepoint
    /**
     * @param h coroutine handle
     * @param tp timepoint
     */
    void schedule(promise<void> &&promise, std::chrono::system_clock::time_point tp) {
        std::lock_guard lk(_mx);;
        _timers.emplace(tp,std::move(promise));
        if (_queue.empty()) {
            _queue.push(std::noop_coroutine());
        }
        _cond.notify_one();
    }
    ///destructor (must be public)
    /**
     * dispatcher's instance is destroyed at the end of the current thread
     */
    ~dispatcher() {
        flush_queue();
    }

    ///retrieves reference to current dispatcher
    /**
     * @return it is always passed as weak pointer to properly handle reference
     * to already finished thread.
     */
    static std::weak_ptr<dispatcher> current() {
        return instance;
    }

    ///suspend coroutine and resume at given time point
    /**
     * @param tp  time point defines time to resume
     * @return awaiter which can be co_awaited
     *
     * @note dispatcher is always one thread, which means, that resumption don't
     * need to be exact at given point. Any running coroutine must be finished
     * first before the sleeping coroutine is resumed. Also note that immediately
     * enqueued coroutines have always priority
     *
     * @note coroutine is resumed in its home dispatcher's thread. This applies for
     * coroutines under resumption_policy::dispatcher. For other coroutines,
     * they are resumed in current thread if there is active dispatcher, otherwise
     * exception is thrown
     */
    static future<void> sleep_until(std::chrono::system_clock::time_point tp) {
        return [tp](auto promise) {
            auto inst = current().lock();
            if (!inst) throw no_thread_dispatcher_is_initialized_exception();
            inst->schedule(std::move(promise), tp);
        };
    }
    ///suspend coroutine and resume at given time point
    /**
     * @param tp  time point defines time to resume
     * @return dur duration, relative to now(), can be any std::chrono::duration
     *
     * @note dispatcher is always one thread, which means, that resumption don't
     * need to be exact at given point. Any running coroutine must be finished
     * first before the sleeping coroutine is resumed. Also note that immediately
     * enqueued coroutines have always priority
     *
     * @note coroutine is resumed in its home dispatcher's thread. This applies for
     * coroutines under resumption_policy::dispatcher. For other coroutines,
     * they are resumed in current thread if there is active dispatcher, otherwise
     * exception is thrown
     */
    template<typename Dur>
    static future<void> sleep_for(const Dur &dur) {
        return sleep_until(std::chrono::system_clock::now()+dur);
    }

    friend bool is_current(dispatcher *disp) {
        return instance.get() == disp;
    }


protected:

    static thread_local std::shared_ptr<dispatcher> instance;

    void run(bool &exit_flag) {
        std::unique_lock lk(_mx);
        for(;;) {
            if (_timers.empty()) {
                _cond.wait(lk, [&]{return !_queue.empty() || exit_flag;});
            } else {
                if (!_cond.wait_until(lk, _timers.top()._tp, [&]{return !_queue.empty() || exit_flag;})) {
                    auto t = _timers.pop_item();
                    lk.unlock();
                    t._coro();
                    lk.lock();
                    continue;
                }
            }
            if (exit_flag) break;
            auto h = _queue.front();
            _queue.pop();
            lk.unlock();
            h.resume();
            lk.lock();
        }
    }

    void flush_queue() {
        std::unique_lock lk(_mx);
        while (!_queue.empty()) {
            auto h = _queue.front();
            _queue.pop();
            lk.unlock();
            h.resume();
            lk.lock();
        }
    }


    void quit(bool &exit_flag) {
        std::unique_lock lk(_mx);
        exit_flag = true;
        _cond.notify_all();
    }


protected:
    struct timer {
        std::chrono::system_clock::time_point _tp;
        promise<void> _coro;
        int operator==(const timer &other) const {return _tp == other._tp;}
        int operator!=(const timer &other) const {return _tp != other._tp;}
        int operator>=(const timer &other) const {return _tp >= other._tp;}
        int operator<=(const timer &other) const {return _tp <= other._tp;}
        int operator>(const timer &other) const {return _tp > other._tp;}
        int operator<(const timer &other) const {return _tp < other._tp;}
    };

    mutable std::mutex _mx;
    std::condition_variable _cond;
    std::queue<std::coroutine_handle<> > _queue;
    priority_queue<timer, std::vector<timer>, std::greater<timer> > _timers;

    static dispatcher * & current_pool() {
        static thread_local dispatcher *c = nullptr;
        return c;
    }
    friend struct resumption_policy::dispatcher;

};

inline thread_local std::shared_ptr<dispatcher> dispatcher::instance;

using dispatcher_ptr = std::weak_ptr<dispatcher>;

namespace resumption_policy {

    ///resumption polici - resume in dispatcher
    /**
     * It ensures, that coroutine is always resumed in thread which was current
     * during coroutine's creation. (the same thread). However that thread need
     * to have a dispatcher installed. If this is not the case, the coroutine
     * won't start, until the dispatcher's instance is passed to the initialize_policy()
     *
     */
    struct dispatcher {

        dispatcher_ptr _dispatcher;

        struct initial_awaiter: std::suspend_always {
            dispatcher &_p;
            initial_awaiter(dispatcher &p):_p(p) {}
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
                auto disp = _p._dispatcher.lock();
                if (!disp) return std::noop_coroutine();
                else return _p.resume_handle(h);
            }
        };


        dispatcher()
            :_dispatcher(dispatcher_ptr::element_type::instance) {}
        dispatcher(dispatcher_ptr d)
            :_dispatcher(d) {}


        bool is_policy_ready() {
            return !_dispatcher.expired();
        }


        dispatcher_ptr get_dispatcher() const {
            return _dispatcher;
        }
          ///resume
          void resume(std::coroutine_handle<> h) {
              auto l = _dispatcher.lock();
              if (l) [[likely]] {
                  l->schedule(h);
                  return;
              }
              throw home_thread_already_ended_exception();
          }
          ///Initializes policy
          /**
           */
          bool initialize_policy(dispatcher_ptr d) {
              bool ret = _dispatcher.expired();
              _dispatcher = d;
              return ret;
          }
          std::coroutine_handle<> resume_handle(std::coroutine_handle<> h) {
              auto l = _dispatcher.lock();
              if (l)  [[likely]] {
                  if (is_current(l.get())) return h;
                  l->schedule(h);
                  return std::noop_coroutine();
              }
              throw home_thread_already_ended_exception();
          }
    };



}




}




#endif /* SRC_COCLASSES_DISPATCHER_H_ */
