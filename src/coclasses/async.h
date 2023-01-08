/*
 * async.h
 *
 *  Created on: 8. 1. 2023
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_ASYNC_H_
#define SRC_COCLASSES_ASYNC_H_

#include <type_traits>
#include <coroutine>
#include <exception>
#include <variant>
#include "awaiter.h"

#include "exceptions.h"

#include "co_alloc.h"
namespace cocls {

///Simplified task-like coroutine. 
/**
 * Async works similar as task, but it has following pros, cons and limitations:
 * 
 * - Async future can be only moved, not copied. 
 * - Async future contains result
 * - Async future can be awaited only by one coroutine at time
 * - Async future can be initialized with result without memory allocation and
 *   without starting a coroutine
 *   
 *    
 */
template<typename T, typename Policy = void>
class async;

namespace _details {

template<typename T, typename Policy>
class async_promise;
template<typename T, typename Policy>
class async_promise_base;
template<typename T, typename Policy>
class async_promise_val;

template<typename T, typename Policy>
class async_base {
public:
    using promise_type = async_promise<T, Policy>;
    using handle = std::coroutine_handle<promise_type>;
    using awaiter = abstract_awaiter<false>;

    async_base():_awaiter(&empty_awaiter<false>::ready_data) {}
    async_base(handle h):_h(h) {
        auto &prom = h.promise();
        prom._result = static_cast<async<T, Policy> *>(this);
    }
    
    async_base(const async_base &other) = delete;
    async_base &operator=(const async_base &other) = delete;

    co_awaiter<async<T, Policy> > operator co_await() {
        return static_cast<async<T, Policy> &>(*this);
    }

    async_base(async_base &&other) {
        other.move_result_to(static_cast<async<T, Policy> *>(this));
        _h = std::exchange(other._h, nullptr);        
        _awaiter = other._awaiter.exchange(&empty_awaiter<false>::disabled, std::memory_order_acquire);
    }
    
    ~async_base() {
        move_result_to(nullptr);
        if (_h && _awaiter.load(std::memory_order_release)) 
            _h.destroy();
    }
    
    void sync() {
        auto awt = operator co_await();
        awt.sync();        
    }
protected:
    friend class async_promise<T, Policy>;
    friend class async_promise_base<T, Policy>;
    friend class co_awaiter<async<T, Policy> >;

    handle _h;
    std::atomic<awaiter *> _awaiter = nullptr;

    bool is_ready() const {
        return awaiter::is_ready(_awaiter);
    }
    
    bool subscribe_awaiter(awaiter *awt) {
        return awt->subscibre_check_ready(_awaiter);
    }

    void move_result_to(async<T, Policy> *to) {
        if (!_awaiter.load(std::memory_order_release)) {
             auto &prom = _h.promise();
             async<T, Policy> *r = static_cast<async<T, Policy> *>(this);
             if (!prom._result.compare_exchange_strong(r, to)) {
                 auto awt = this->operator co_await();
                 awt.sync();
             }
         }        
    }
    
};

}

template<typename T, typename Policy>
class async: public _details::async_base<T, Policy> {
public:
    using promise_type = _details::async_promise<T, Policy>;
    using handle = std::coroutine_handle<promise_type>;
    using awaiter = abstract_awaiter<false>;
    
    
    async(handle h):_details::async_base<T, Policy>(h) {}
    async(const T &value): _value(value) {}
    async(T &&value):_value(std::move(value)) {}
    async(std::exception_ptr e):_value(e) {}
    
    async(const async &other) = delete;
    async &operator=(const async &other) = delete;
    
    async(async &&other):_details::async_base<T, Policy>(std::move(other)) {
        if (this->_awaiter.load(std::memory_order_relaxed)) {
            _value = std::move(other._value);
        }
    }
    
    T &join() {
        sync();
        return get_result();
    }
    
protected:
    friend class _details::async_promise<T, Policy>;
    friend class _details::async_promise_val<T, Policy>;
    friend class co_awaiter<async<T, Policy> >;
    
    std::variant<std::monostate, T, std::exception_ptr> _value;
    
    T &get_result() {
        if (std::holds_alternative<T>(_value)) {
            return std::get<T>(_value);
        } else if (std::holds_alternative<std::exception_ptr>(_value)) {
            std::rethrow_exception(std::get<std::exception_ptr>(_value));
        }
        throw value_not_ready_exception();
    }
    
    
};

template<typename Policy>
class async<void, Policy>: public _details::async_base<void, Policy> {
public:
    using promise_type = _details::async_promise<void, Policy>;
    using handle = std::coroutine_handle<promise_type>;
    using awaiter = abstract_awaiter<false>;
    
    
    async(handle h):_details::async_base<void, Policy>(h) {}
    async() = default;
    async(std::exception_ptr e):_e(e) {}
    
    async(const async &other) = delete;
    async &operator=(const async &other) = delete;
    
    async(async &&other) = default;
    
    void join() {
        sync();
        get_result();
    }
    
protected:
    friend class _details::async_promise<void, Policy>;
    friend class _details::async_promise_val<void, Policy>;
    friend class co_awaiter<async<void, Policy> >;
    
    std::exception_ptr _e;
    
    void get_result() {
        if (_e) std::rethrow_exception(_e);
    }
    
    
};

namespace _details {



template<typename T, typename Policy>
class async_promise_base: public coro_allocator {
public:
    using awaiter = abstract_awaiter<false>;
    std::atomic<async<T, Policy> *> _result = nullptr;
    async<T, Policy> *_claimed = nullptr;
    

    async_promise_base() =default;
    async_promise_base(async_promise_base &) =delete;
    async_promise_base &operator=(async_promise_base &) =delete;
    
    static std::suspend_never initial_suspend() {return {};}
    
    struct final_suspender: std::suspend_always {
        bool await_suspend(std::coroutine_handle<async_promise<T, Policy> > h) noexcept {
            auto &prom = h.promise();
            if (prom._claimed) {
                awaiter::mark_ready_data_resume(prom._claimed->_awaiter);
                return true;
            } else {
                return false;
            }
        }
    };
    
    static final_suspender final_suspend() noexcept {return {};}


};


template<typename T, typename Policy>
class async_promise_val: public async_promise_base<T, Policy> {
public:
    using awaiter = abstract_awaiter<false>;
    
    
    void unhandled_exception() {
        this->_claimed = this->_result.exchange(nullptr);
        if (this->_claimed) {
            this->_claimed->_value = std::current_exception();
        }
    }

    void return_value(T &&x) {
        this->_claimed = this->_result.exchange(nullptr);
        if (this->_claimed) {
            this->_claimed->_value = std::move(x);
        }
    }

};


template<typename Policy>
class async_promise_val<void,Policy>: public async_promise_base<void,Policy>{
public:
    using awaiter = abstract_awaiter<false>;
    
    std::atomic<async<void, Policy> *> _result = nullptr;
    async<void, Policy> *_claimed = nullptr;
    
    
    void unhandled_exception() {
        _claimed = _result.exchange(nullptr);
        if (_claimed) {
            _claimed->_e = std::current_exception();
        }
    }

    void return_void() {
        _claimed = _result.exchange(nullptr);
    }

};

template<typename T, typename Policy>
class async_promise: public async_promise_val<T, Policy> {
public:
    using P = std::conditional_t<std::is_void_v<Policy>, typename resumption_policy::unspecified<void>, Policy>;

    template<typename Awt>
    decltype(auto) await_transform(Awt&& awt) noexcept {
        if constexpr (has_co_await<Awt>::value) {
            auto x = await_transform(awt.operator co_await());
            return x;
        } else if constexpr (has_global_co_await<Awt>::value) {
            auto x = await_transform(operator co_await(awt));
            return x;
        } else if constexpr (has_set_resumption_policy<Awt, P>::value) {
            return awt.set_resumption_policy(std::forward<Awt>(awt), _policy);
        } else {
            return std::forward<Awt>(awt);
        }
    }

    using initial_awaiter = typename std::remove_reference<P>::type::initial_awaiter;

    initial_awaiter initial_suspend()  noexcept {
        return initial_awaiter(_policy);
    }

    template<typename ... Args>
    void initialize_policy(Args &&... args) {
        _policy.initialize_policy(std::forward<Args>(args)...);
    }

    async<T, Policy> get_return_object() {
        return async<T, Policy>(std::coroutine_handle<async_promise>::from_promise(*this));
    }

    
    [[no_unique_address]]  P _policy;

};

}


}

#endif /* SRC_COCLASSES_ASYNC_H_ */
