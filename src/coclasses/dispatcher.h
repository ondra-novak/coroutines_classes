/** @file dispatcher.h
 * 
 */
#pragma once
#ifndef SRC_COCLASSES_DISPATCHER_H_
#define SRC_COCLASSES_DISPATCHER_H_
#include "abstract_awaiter.h"

#include "lazy.h"

#include "no_alloc.h"

#include <memory>
#include <mutex>
#include <queue>
#include <type_traits>

namespace cocls {

namespace resumption_policy {    
    struct dispatcher;
}

class no_thread_dispatcher_is_initialized_exception: public std::exception {
public:
    const char *what() const noexcept override {
        return "No thread's dispatcher is initialized on the current thread";
    }
};

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
     * all enqueued coroutines are processed, it starts to wait for newly nequeued coroutines
     * until the awaiter becomes signaled. Then exits and returns value.
     *  
     * @param awt an expression which results to an awaiter to be awaiter
     * @return return value of the expression
     */
    template<typename Awt>
    static decltype(auto) await(Awt &&awt) {
        if constexpr(has_co_await<Awt>::value) {
            return await(awt.operator co_await());
        } else {
            if (awt.await_ready()) {
                return awt.await_resume();
            } 
            if (instance == nullptr) throw no_thread_dispatcher_is_initialized_exception();
            no_alloc<lazy<>, storage_t<> > t = ([](storage_t<> &,Awt &&awt, std::shared_ptr<dispatcher> inst)->lazy<>{
                inst->quit();
                co_return;
            })(instance->_watcher_storage, std::forward<Awt>(awt),instance);
            auto h = t.get_start_handle();
            
            if constexpr(std::is_same<decltype(awt.await_suspend(h)),bool>::value) {
                if (!awt.await_suspend(h)) {
                    instance->enqueue(h);
                }
            } else if constexpr(std::is_same<decltype(awt.await_suspend(h)),std::coroutine_handle<> >::value) {
                instance->enqueue(awt.await_suspend(h));
            } else {
                awt.await_suspend(h);
            }
            
            instance->run();
                
            return awt.await_resume();
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

    ///enqueue coroutine to run in the dispatcher's thread
    /**
     * @param h coroutine handle
     */
    void enqueue(std::coroutine_handle<> h) {
        std::lock_guard lk(_mx);;
        _queue.push(h);
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
    
protected:
    
    static thread_local std::shared_ptr<dispatcher> instance;
    storage_t<> _watcher_storage;


    
    void run() {
        std::unique_lock lk(_mx);
        _exit = false;
        for(;;) {
            _cond.wait(lk, [&]{return !_queue.empty() || _exit;});
            if (_exit) break;
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
    
    
    void quit() {
        std::unique_lock lk(_mx);
        _exit = true;
        _cond.notify_all();
    }
    
protected:
    mutable std::mutex _mx;
    std::condition_variable _cond;
    std::queue<std::coroutine_handle<> > _queue;
    bool _exit = false;    
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
     * If the coroutine need to be resumed after the original thread is  
     */ 
    struct dispatcher {

        std::variant<std::monostate, dispatcher_ptr, std::coroutine_handle<> > _st; 
        
        dispatcher()
            :_st(dispatcher_ptr::element_type::instance) {}
        dispatcher(dispatcher_ptr d) {
            initialize_policy(d);
        }

        
          ///resume
          void resume(std::coroutine_handle<> h) {
              if (_st.index() == 1) [[likely]] {
                  auto l = std::get<dispatcher_ptr>(_st).lock();
                  if (l) [[likely]] {
                      l->enqueue(h);
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
          

    };
    
    

}


}




#endif /* SRC_COCLASSES_DISPATCHER_H_ */
