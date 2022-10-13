#pragma once
#ifndef SRC_COCLASSES_SCHEDULER_H_
#define SRC_COCLASSES_SCHEDULER_H_
#include "task.h"

#include "thread_pool.h"

#include "generator.h"

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <mutex>
#include <queue>


namespace cocls {

///Scheduler - schedules resumption of coroutines at given time, or suspension for given duration
/**
 * Sheduler is object, which exposes function sleep_for and sleep_until that can
 * be co_await-ed. During waiting for the specified time, coroutine is suspeneded
 * 
 * 
 * @tparam Clock specifies clock type. Default is std::chrono::system_clock
 * 
 * @note scheduler has no thread. You need to supply a thread_pool instance. 
 * Scheduler need minimal 1 thread. More threads allows to resume more coroutines
 * scheduled on same time point, while each coroutine is resumed in different thread
 * 
 * You can cancel scheduled coroutines
 */


template<typename Clock>
struct scheduler_traits;

template<typename Clock = std::chrono::system_clock, typename Traits = scheduler_traits<Clock> >
class scheduler {
public:
    using timepoint = typename Traits::timepoint;
    using clock = Clock;
    
    
    ///Initialize scheduler, but don't start it now
    scheduler() = default;
    ///Initialize scheduler and start it at given thread pool
    /**
     * @param pool selected thread pool
     */
    scheduler(thread_pool &pool) { start(pool);}
    ///Don't copy
    scheduler(const scheduler &other) = delete;
    ///Don't assign
    scheduler &operator=(const scheduler &other) = delete;
    
    ///Start scheduler on given thread pool.
    /**
     * @param pool selected thread pool
     * @note starting scheduler more then once is undefined behavior
     */
    void start(thread_pool &pool) {
        _worker = worker(pool);
    }
    
    using timer_id = void *;
    

    ///awaiter
    class awaiter {
    public:
        awaiter(scheduler &owner, timepoint tp):_owner(owner),_tp(tp) {}
        awaiter(const awaiter &) = default;
        awaiter &operator=(const awaiter &) = delete;
        
        bool await_ready() const {
            return _tp<Traits::now() || _owner._exit;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            std::lock_guard _(_owner._mx);
            if (_owner._exit) return h;
            _h = h;
            _owner._list.push({_tp,this});
            _owner._signal = true;
            _owner._sleeper.notify_one();
            return resume_lock::await_suspend();
        }
        
        timer_id await_resume() const {
            if (_canceled) throw await_canceled_exception();
            return _h.address();
        }
        
        void resume(bool canceled) {
            _canceled = canceled;
            resume_lock::resume(_h);
        }
        bool is(timer_id id) {
            return _h.address() == id;
        }
        
    protected:
        scheduler &_owner;
        timepoint _tp;
        std::coroutine_handle<> _h;
        bool _canceled = false;
    };
 
    ///suspend coroutine until given timepoint
    /**
     * @param tp timepoint. If the timepoint is in the pass, coroutine is not suspended
     * @return awaiter
     */
    awaiter sleep_until(const timepoint &tp) {
        return awaiter(*this, tp);
    }
    
    ///suspend coroutine for given duration
    /**
     * @param dur duration object must be compatibilite with given Clock. For 
     *  std::chrono::system_clock, you can use std::chrono::duration 
     * 
     * @return awaiter
     */
    template<typename Dur, typename = decltype(Traits::from_duration(std::declval<Dur>()))>
    awaiter sleep_for(const Dur &dur) {
        return awaiter(*this, Traits::from_duration(dur));
    }
    
    
    ///Generator of intervals
    /**
     * @param dur duration.
     * @return generator, increases value for every cycle by one, so first result is
     * 0, second is 1, next is 2, 3, etc... 
     * 
     * @note starting point is defined by first co_await. Generator must be 
     * awaited again to continue. However, the time spend between co_yield and next co_await
     * on it is substracted from the next interval
     * 
     * 
     */
    template<typename Dur, typename = decltype(add(std::declval<timepoint>(), std::declval<Dur>()))>
    generator<unsigned int> interval(Dur dur) {
        unsigned int n = 0;
        timepoint nexttp = Traits::from_duration(dur);
        try {
            for(;;) {
                co_await sleep_until(nexttp);
                nexttp = Traits::from_duration(dur);
                co_yield n++;
            }
        } catch (await_canceled_exception &) {
            // exit;
        }
        
    }
    
    ///cancel waiting for scheduled coroutine
    /**
     * @param id coroutine identifier to cancel
     * @retval true coroutine canceled
     * @retval false coroutine is not scheduled at the time
     * 
     * @note canceled coroutine receives exception await_canceled_exception
     */
    bool cancel(const timer_id &id);
    

    ///cancel waiting task
    /**
     * @param task task to cancel
     * @retval true coroutine canceled
     * @retval false coroutine is not scheduled at the time
     * 
     * @note canceled coroutine receives exception await_canceled_exception
     */
    template<typename T>
    bool cancel(const task<T> &task_) {
        return cancel(task_.get_id());
    }
    ///cancel waiting generator
    /**
     * @param generator generator to cancel
     * @retval true coroutine canceled
     * @retval false coroutine is not scheduled at the time
     * 
     * @note canceled coroutine receives exception await_canceled_exception
     */
    template<typename T>
    bool cancel(const generator<T> &generator_) {
        return cancel(generator_.get_id());
    }
    
    void stop() {        
        {
            std::lock_guard _(_mx);
            _exit = true;
            _sleeper.notify_all();
        }
        _worker.join();        
        while (!_list.empty()) {
            _list.top().aw->resume(true);
            _list.pop();
        }
    }
    
    ~scheduler() {
        stop();
    }

protected:
    
    struct sch_item_t {
        timepoint tp;
        awaiter *aw;
        bool operator<(const sch_item_t &a) const {return tp < a.tp;} 
        bool operator>(const sch_item_t &a) const {return tp > a.tp;} 
        bool operator<=(const sch_item_t &a) const {return tp <= a.tp;} 
        bool operator>=(const sch_item_t &a) const {return tp >= a.tp;} 
        bool operator!=(const sch_item_t &a) const {return tp != a.tp;} 
        bool operator==(const sch_item_t &a) const {return tp == a.tp;} 
    };
    
    using sch_list_t = std::priority_queue<sch_item_t,  std::vector<sch_item_t>, std::greater<sch_item_t> >;

    sch_list_t _list;    
    std::vector<std::coroutine_handle<> > _canceled;
    std::mutex _mx;
    typename Traits::sleeper _sleeper;
    bool _exit = false;
    bool _signal = false;
    task<> _worker;
    
    
    task<> worker(thread_pool &pool) {
        try {
            do {
                awaiter *aw = get_expired();
                if (aw) {
                    co_await pool.fork([aw]{
                        aw->resume(false);
                    });
                } else {
                    co_await pool;
                    wait();
                }
            } while (!_exit);
        } catch (await_canceled_exception &) {
            //exit now
        }
    }
    
    awaiter *get_expired() {
        std::lock_guard _(_mx);
        if (_list.empty()) return nullptr;
        auto t = Traits::now();
        if (_list.top().tp < t) {
            awaiter *aw = _list.top().aw;
            _list.pop();
            return aw;
        }
        return nullptr;
    }
    
    void wait() {
        std::unique_lock _(_mx);
        while (_list.empty() && !_exit) {
            _signal = false;
            _sleeper.wait(_, [&]{return _signal || _exit;});            
        } 
        if (!_exit) {
            _sleeper.wait_until(_,_list.top().tp, [&]{return _signal || _exit;});
        }
        _signal = false;
    }
};

template<typename Clock>
struct scheduler_common_traits {
    using timepoint = typename Clock::time_point;
    static timepoint now() {return  Clock::now();}
    static timepoint tp_max() {return timepoint::max();}
    static timepoint tp_min() {return timepoint::min();}
    
    template<typename Dur>
    static timepoint from_duration(Dur &&dur) {return now() + dur;}
    class sleeper {
        template<typename Pred>
        void wait_until(std::mutex &_mx, const timepoint &tp, Pred &&pred);        
        template<typename Pred>
        void wait(std::mutex &_mx, Pred &&pred );
        void notify();
    };
  
};

template<>
struct scheduler_traits<std::chrono::system_clock>: scheduler_common_traits<std::chrono::system_clock> {
    using sleeper = std::condition_variable;
};


template<typename Clock, typename Traits>
inline bool scheduler<Clock, Traits>::cancel(const timer_id &id) {
    std::unique_lock _(_mx);
    sch_list_t oldls;
    std::swap(oldls, _list);    
    while (!oldls.empty()) {
        if (oldls.top().aw->is(id)) {
            awaiter *aw = oldls.top().aw;
            oldls.pop();
            while (!oldls.empty()) {
                _list.push(std::move(oldls.top()));
                oldls.pop();
            }
            _.unlock();
            aw->resume(true);
            return true;
        } else {
            _list.push(std::move(oldls.top()));
            oldls.pop();            
        }
    }
    return false;
}

}
#endif /* SRC_COCLASSES_SCHEDULER_H_ */
