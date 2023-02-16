/** @file dispatcher.h
 *
 */
#pragma once
#ifndef SRC_COCLASSES_DISPATCHER_H_
#define SRC_COCLASSES_DISPATCHER_H_
#include "awaiter.h"
#include "exceptions.h"


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
    void schedule(std::coroutine_handle<> h, std::chrono::system_clock::time_point tp) {
        std::lock_guard lk(_mx);;
        _timers.push({tp,h});
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

    ///sleep awaiter, coroutine can be scheduled in dispatcher thread at given timepoint
    /**@see sleep_until(), sleep_for()
     */
    class sleep_awaiter: public std::suspend_always {
    public:
        sleep_awaiter(std::weak_ptr<dispatcher> disp, std::chrono::system_clock::time_point tp):_disp(disp),_tp(tp) {}
        void await_suspend(std::coroutine_handle<> h) {
            auto inst = current().lock();
            if (!inst) throw no_thread_dispatcher_is_initialized_exception();
            inst->schedule(h, _tp);
        }
        static sleep_awaiter set_resumption_policy(const sleep_awaiter &awt, const resumption_policy::dispatcher &policy);
    protected:
        std::weak_ptr<dispatcher> _disp;
        std::chrono::system_clock::time_point _tp;
    };

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
    static sleep_awaiter sleep_until(std::chrono::system_clock::time_point tp) {
        return sleep_awaiter(instance,tp);
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
    static sleep_awaiter sleep_for(const Dur &dur) {
        return sleep_awaiter(instance,std::chrono::system_clock::now()+dur);
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
                    auto t = _timers.top();
                    _timers.pop();
                    lk.unlock();
                    t._coro.resume();
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
        while (!_timers.empty()) {
            auto t = _timers.top();
            _timers.pop();
            lk.unlock();
            t._coro.resume();
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
        std::coroutine_handle<> _coro;
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
    std::priority_queue<timer, std::vector<timer>, std::greater<timer> > _timers;

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

        std::variant<std::monostate, dispatcher_ptr, std::coroutine_handle<> > _st;

        struct initial_awaiter: initial_resume_by_policy<dispatcher> {
            using initial_resume_by_policy<dispatcher>::initial_resume_by_policy;
            bool await_ready() const {
                return _p._st.index() == 1;
            }
        };


        dispatcher()
            :_st(dispatcher_ptr::element_type::instance) {}
        dispatcher(dispatcher_ptr d) {
            initialize_policy(d);
        }


        dispatcher_ptr get_dispatcher() const {
            return std::get<dispatcher_ptr>(_st);
        }
          ///resume
          void resume(std::coroutine_handle<> h) {
              if (_st.index() == 1) [[likely]] {
                  auto l = std::get<dispatcher_ptr>(_st).lock();
                  if (l) [[likely]] {
                      l->schedule(h);
                      return;
                  }
                  throw home_thread_already_ended_exception();
              }
              _st = h;
          }
          ///Initializes policy
          /**
           */
          void initialize_policy(dispatcher_ptr d) {
              if (_st.index() == 2) [[likely]] {
                  std::coroutine_handle<> h = std::get<std::coroutine_handle<> >(_st);
                  _st = d;
                  resume(h);
              } else {
                  _st = d;
              }
          }
          std::coroutine_handle<> resume_handle(std::coroutine_handle<> h) {
              if (_st.index() == 1) [[likely]] {
                  auto l = std::get<dispatcher_ptr>(_st).lock();
                  if (l)  [[likely]] {
                      auto k = dispatcher::get_dispatcher().lock();
                      if (k == l) return h;
                      l->schedule(h);
                      return std::noop_coroutine();
                  }
                  throw home_thread_already_ended_exception();
              }
              _st = h;
              return std::noop_coroutine();
          }




    };



}


inline dispatcher::sleep_awaiter dispatcher::sleep_awaiter::set_resumption_policy(const dispatcher::sleep_awaiter &awt, const resumption_policy::dispatcher &policy) {
    return dispatcher::sleep_awaiter(policy.get_dispatcher(),awt._tp);
}


}




#endif /* SRC_COCLASSES_DISPATCHER_H_ */
