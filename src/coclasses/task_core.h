#pragma once
#ifndef SRC_COCLASSES_TASK_CORE_H_
#define SRC_COCLASSES_TASK_CORE_H_

#include "common.h" 
#include <atomic>
#include <coroutine>


namespace cocls {


template<typename Owner> class task_awaiter {
public:
    task_awaiter(Owner &owner): _owner(owner) {}
    
    task_awaiter(const task_awaiter &other) = delete;
    task_awaiter &operator=(const task_awaiter &other) = delete;
    
    bool await_ready() const noexcept {
        return _owner.is_ready();
    }
    bool await_suspend(std::coroutine_handle<> h) {
        _h = h;
        return _owner.register_awaiter(this);
    }
    auto &await_resume() {
        return _owner.get_value();
    }
    void resume() {
        _h.resume();
    }

    void push_to(std::atomic<task_awaiter<Owner> *> &list) {
        while (!list.compare_exchange_weak(next, this));
    }
    
    task_awaiter *get_next() {
        return next;
    }
protected:
    task_awaiter *next = nullptr;
    Owner &_owner;
    std::coroutine_handle<> _h;
    

};

template<typename Impl> class task_promise_base {
public:
    
    task_promise_base():_ready(false),_awaiters(nullptr) {}

    void resolve(task_awaiter<Impl> *dont_resume) {
        task_awaiter<Impl> *list = _awaiters.exchange(nullptr);
        while (list) {
            auto *p = list;
            list = list->get_next();
            if (p != dont_resume) p->resume();            
        }
    }
    
    void resolve() {
        resolve(nullptr);
    }
    
    bool is_ready() const {
        return _ready;
    }

    bool register_awaiter(task_awaiter<Impl> *ptr) {
        if (_ready) return false;
        ptr->push_to(_awaiters);
        if (_ready) {
            resolve(ptr);
            return false;
        }
        if (ptr->get_next() == nullptr) {          
            //support lazy<> notifies promise, that first awaiter was registered
            static_cast<Impl *>(this)->on_first_awaiter();
        }
        return true;
    }
    
    auto &get_value() {
        return static_cast<const Impl &>(*this).get_value();
    }
    
    void on_first_awaiter() {
        //this is empty here, but promise can overwrite the code
    }
    
protected:
    std::atomic<bool> _ready;
    std::atomic<task_awaiter<Impl> *> _awaiters;
};

template<typename Impl> class task_coroutine_base {
public:
    task_coroutine_base():_ref_count(1) {}

    task_coroutine_base(const task_coroutine_base &) = delete;
    task_coroutine_base &operator=(const task_coroutine_base &) = delete;

    ///handles final_suspend
    class final_awaiter {
    public:
        final_awaiter(task_coroutine_base &prom): _owner(prom) {}        
        
        final_awaiter(const final_awaiter &prom) = default;
        final_awaiter &operator=(const final_awaiter &prom) = delete;
        
        bool await_ready() const noexcept {
            return _owner._ref_count == 0;
        }
        constexpr void await_suspend(std::coroutine_handle<>) const noexcept {}
        constexpr void await_resume() const noexcept {}        
    protected:
        task_coroutine_base &_owner;
    };
    
    std::suspend_never initial_suspend() const noexcept {return {};}
    final_awaiter final_suspend() noexcept {
        static_cast<Impl *>(this)->resolve();
        --_ref_count;
        return *this;
    }
    
    void add_ref() {
        _ref_count++;
    }
    void release_ref() {
        if (--_ref_count == 0) {
            Impl &me = static_cast<Impl &>(*this);
            auto h = std::coroutine_handle<Impl >::from_promise(me);
            h.destroy();
        }
    }

    
protected:
    std::atomic<unsigned int> _ref_count;
};

}



#endif /* SRC_COCLASSES_TASK_CORE_H_ */
