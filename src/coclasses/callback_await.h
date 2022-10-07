#pragma once
#ifndef SRC_COCLASSES_CALLBACK_AWAIT_H_
#define SRC_COCLASSES_CALLBACK_AWAIT_H_

#include "resume_lock.h"

namespace cocls {


template<typename X>
struct call_resume_fn {
    template<typename Fn, typename Awaiter>
    static void call(Fn &&fn, Awaiter &aw) {
        fn(aw.await_resume());
    }
};

template<>
struct call_resume_fn<void> {
    template<typename Fn, typename Awaiter>
    static void call(Fn &&fn, Awaiter &aw) {
        aw.await_resume();
        fn();
    }
};




template<typename Awaiter>
class callback_await_sup {
public:
    callback_await_sup(Awaiter &&aw):_aw(std::forward<Awaiter>(aw)) {}
    callback_await_sup(const callback_await_sup &) = delete;
    callback_await_sup &operator=(const callback_await_sup &) = delete;
    
    template<typename OkFn, typename ExceptFn> 
    struct handlers {
        OkFn ok;
        ExceptFn except;
    };

    using value_type = decltype(std::declval<Awaiter>().await_resume());

    
    template<typename OkFn, typename ExceptFn>
    void then(OkFn &&_1, ExceptFn &&_2) {
        handlers<OkFn,ExceptFn> h{std::forward<OkFn>(_1), std::forward<ExceptFn>(_2)};
        try {
            if (_aw.await_ready()) {
                after_resume(h,_aw);
            } else {
                suspend_callback(std::move(h)); 
            }
        } catch (...) {
            after_resume_exception(h);
        }
    }
    
    
    
protected:
    Awaiter _aw;
        
  
    template<typename H>
    static std::coroutine_handle<> after_resume(H &h, Awaiter &aw) {
        try {
            if constexpr (std::is_void<decltype(call_resume_fn<value_type>::call(h.ok, aw))>::value) {
                call_resume_fn<value_type>::call(h.ok, aw);
                return std::noop_coroutine();
            } else {
                return call_resume_fn<value_type>::call(h.ok, aw);
            }
        } catch (...) {
            return after_resume_exception(h);
            
        }            
        
    }
    
    template<typename H>
    static std::coroutine_handle<> after_resume_exception(H &h) {
        if constexpr (std::is_void<decltype(h.except())>::value) {
            h.except();
            return std::noop_coroutine();
        } else {
            return h.except();
        }
    }

    
    template<typename H>
    class instance: public abstract_resumable_t {
    public:
        instance(H &&h, Awaiter &aw):_h(std::forward<H>(h)),_aw(std::forward<Awaiter>(aw)) {}
        virtual std::coroutine_handle<> resume() noexcept override {
            auto r = after_resume(_h,_aw);
            delete this;
            return r;
        }
        void finish_suspend() noexcept {
            try {
                resume_lock::resume(_aw.await_suspend(handle_t(this)));
            } catch (...) {
                after_resume_exception(_h);
                delete this;
            }
        }
        
    protected:
        H _h;
        Awaiter _aw;
        
    };
    
    template<typename OkFn, typename ExceptFn>
    void suspend_callback(handlers<OkFn, ExceptFn> &&h) {
        auto inst = new instance<handlers<OkFn, ExceptFn> >(std::move(h), _aw);
        inst->finish_suspend();        
    }
};

///callback await
/**
 * @code
 *  callback_await(expr).then(
 *          [](auto &x){... result ....},
 *          [](){try {
 *                  throw;
 *              } catch (...) {
 *                  
 *              }
 *              });
 *
 * @endcode  
 * 
 */
template<typename Expr>
auto callback_await(Expr &&x) -> decltype(x.operator co_await().await_resume(),callback_await_sup(x.operator co_await())) {
    return callback_await_sup(x.operator co_await());
}
///callback await
template<typename Expr>
auto callback_await(Expr &&x) -> decltype(x.await_resume(),callback_await_sup(x)) {
    return callback_await<Expr>(std::forward<Expr>(x));
}

}



#endif /* SRC_COCLASSES_CALLBACK_AWAIT_H_ */
