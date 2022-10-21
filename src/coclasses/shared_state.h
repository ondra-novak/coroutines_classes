#pragma once
#ifndef SRC_COCLASSES_SHARED_STATE_H_
#define SRC_COCLASSES_SHARED_STATE_H_
#include "abstract_awaiter.h"

#include "mutex.h"

#include "exceptions.h"

#include <atomic>


namespace cocls {

namespace _details {

template<typename T>
class shared_state {
public:
    
    shared_state(T &&p):_payload(std::move(p)) {}
    
    template<typename ... Args>
    shared_state(Args && ... args):_payload(std::forward<Args>(args)...) {}
    
    bool subscribe_awaiter(abstract_awaiter<true> *awt) {
        awt->subscribe(_awaiters);
        if (_counter.fetch_sub(1, std::memory_order_relaxed)-1 == 0) {
            release_awaiters(awt);
            return false;                    
        } else {
            return true;
        }
    }
    void add_ref() {
        _counter.fetch_add(1, std::memory_order_relaxed);
    }
    bool release_ref() {
        return _counter.fetch_sub(1, std::memory_order_release)-1 == 0;
    }
    
    decltype(auto) lock() {return _lock.lock();}
    
    bool release_awaiters(abstract_awaiter<true> *skip = nullptr) {
        auto awt = _awaiters.exchange(nullptr, std::memory_order_acquire);
        std::size_t cnt = 0;
        for (auto x = awt; x; x = x->_next) cnt++;
        if (_counter.fetch_add(cnt)+cnt == 0) return true;
        abstract_awaiter<true>::resume_chain_lk(awt, skip);
        return false;
    }
    
    T &get_payload() {return _payload;}
    T *get_payload_ptr() {return &_payload;}
    
protected:
    T _payload;
    std::atomic<std::size_t> _counter = 0;
    std::atomic<abstract_awaiter<true> *> _awaiters = nullptr;
    mutex _lock;   
};

}

///shared state is tool which allows to share a state between many threads or coroutines.
/**
 * The state can be shared by copying, moved, etc. It can be awaited, then coroutine
 * is resumed once all instances of the shared state is released (expected that one,
 * which is being awaited). Result of co_await is reference to the shared state
 * 
 * There can be multiple co_awaits
 * 
 * Shared state can be also access through -> or *. There is also mutex, which can
 * be locked to ensure, that nobody accessing the shared state.
 * 
 * @tparam T type to be shared
 * 
 * @code
 * auto state = make_shared_state(payload);
 * for (int i = 0; i < 10; i++) {
 *      run_threaded_task(state);
 * }
 * auto result = co_await state;
 * @endcode
 * 
 * 
 * 
 */
template<typename T>
class shared_state {
public:
    ///can be created empty
    shared_state();
    ///can be copied
    shared_state(const shared_state &other):_ptr(other._ptr) {if (_ptr) [[likely]] _ptr->add_ref();}
    ///can be moved
    shared_state(shared_state &&other):_ptr(other._ptr) {other._ptr = nullptr;}
    ///can be assigned
    shared_state &operator=(const shared_state &other) {
        if (&other != this) [[likely]]  {
            release();
            _ptr = other._ptr;
            if (_ptr) [[likely]] _ptr->add_ref();
        }
        return *this;
    }
    ///can be moved by assign
    shared_state &operator=(shared_state &&other) {
        if (&other != this) [[likely]]  {
            release();
            _ptr = other._ptr;
            other._ptr = nullptr;
        }
        return *this;
    }

    ///release the content of the object, which is the same a destructor
    void release() {
        auto p = _ptr;
        _ptr = nullptr;
        if (p) [[likely]]  {
            if (p->release_ref()) {
                if (p->release_awaiters()) {
                    delete p;
                }
            }
        }
    }
    
    ///destruction, decrease reference
    ~shared_state() {
        release();
    }

    ///await on state
    co_awaiter<shared_state,true> operator co_await() {
        return *this;
    }
    
    ///lock shared state
    decltype(auto)  lock() const {
        return _ptr->lock();
    }
    
    ///access shared state
    T *operator->() const {
        return _ptr->get_payload_ptr();
    }
    ///access shared state
    T &operator *() const {
        return _ptr->get_payload();
    }
    
protected:
    _details::shared_state<T> *_ptr;
    
    shared_state (_details::shared_state<T> *ptr):_ptr(ptr) {_ptr->add_ref();}
    
    friend class co_awaiter<shared_state, true>;
    
    bool is_ready() {        
        return _ptr == nullptr;    //can't be ready immediately unless it is nullptr
    }
    
    bool subscribe_awaiter(abstract_awaiter<true> *awt) {
        return _ptr->subscribe_awaiter(awt);
        
    }
    
    T &get_result() {
        if (_ptr) {
            return _ptr->get_payload();
        } else {
            throw no_longer_avaible_exception();
        }
    }
    
    template<typename A, typename ...Args>
    friend shared_state<A> make_shared_state(Args && ... args);
    template<typename A>
    friend shared_state<A> make_shared_state(A &&args);
    
public:    
    
};


template<typename T, typename ... Args>
shared_state<T> make_shared_state(Args && ... args) {
    return shared_state<T>(new _details::shared_state<T>(std::forward<Args>(args)...));
}
template<typename T>
shared_state<T> make_shared_state(T &&inst) {
    return shared_state<T>(new _details::shared_state<T>(std::move(inst)));
}

}




#endif /* SRC_COCLASSES_SHARED_STATE_H_ */
