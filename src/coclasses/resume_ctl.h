#ifndef SRC_COCLASSES_RESUME_CTL_H_
#define SRC_COCLASSES_RESUME_CTL_H_

#include <coroutine>
#include <queue>
#include <type_traits>

namespace cocls {

template<typename Awt>
class resume_ctl_awaiter;


///Resume control object
/**
 * Resume control object is thread_local object created when the first task is started. It tracks
 * which coroutine is currently running and performs symmetric transfer between coroutines when
 * co_await is used. It contains a queue coroutines ready to run.
 *
 * @b Rules
 *
 * - a non-coroutine can resume any coroutine (resume_ctl::resume)
 * - a coroutine can't resume other coroutine
 * - a non-coroutine can start a new coroutine, the newly created coroutine is executed immediately
 * - a coroutine can start a new coroutine as well, however the newly created
 *          coroutine is not executed immediately, it is executed on first co_await
 *
 */
class resume_ctl {
public:

    ///Retrieve first ready coroutine to be resumed in an awaiter function await_suspend()
    /**
     * Call this function in awaiter::await_suspend(), returned value must be returned from that function.
     * @return coroutine ready to run, you have to resume it.
     *
     * @note useful in awaiter::await_suspend
     */
    [[nodiscard]] static  std::coroutine_handle<> await_suspend() {
        return instance().await_suspend_impl();
    }

    ///Enqueue specified coroutine to the queue a retrieve other coroutine from the queue
    /**
     * @param h coroutine recently suspended.
     * @return coroutine to resume.
     *
     * @note useful in awaiter::await_suspend
     */
    [[nodiscard]] static std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        return instance().await_suspend_impl(h);
    }

    ///Resume specified coroutine or prepare it for resumption
    /** If the code is called from a coroutine, the specified coroutine is only prepared
     *
     * If the code is called from normal code, the specified coroutine si resumed immediately
     *
     * */
    static void resume(std::coroutine_handle<> h) {
        return instance().resume_impl(h);
    }

    ///Wraps any awaiter's await_suspend function by function which can comunnicate with resume_ctl
    /**
     * This function is called from awaiter generated by transform_awaiter
     * @param awt original awaiter
     * @param h handle
     * @return coroutine to resume
     */
    template<typename Awt>
    static std::coroutine_handle<> await_suspend_awt(Awt &&awt, std::coroutine_handle<> h) noexcept {
        if constexpr(std::is_void<decltype(awt.await_suspend(h))>::value) {
            awt.await_suspend(h);
            return resume_ctl::await_suspend();
        } else if constexpr(std::is_same<decltype(awt.await_suspend(h)), bool>::value) {
            if (awt.await_suspend(h)) {
                return resume_ctl::await_suspend();
            } else {
                return h;
            }
        } else if constexpr(std::is_same<decltype(awt.await_suspend(h)), std::coroutine_handle<> >::value) {
            auto k = awt.await_suspend(h);
            if (k == std::noop_coroutine()) {
                return resume_ctl::await_suspend();
            } else {
                return k; 
            }
        } else {           
            return resume_ctl::await_suspend();
        }
    }

protected:
    static resume_ctl &instance() {
        static thread_local resume_ctl inst;
        return inst;
    }

    std::coroutine_handle<> await_suspend_impl() {
        if (_q.empty()) {
            return std::noop_coroutine();
        }

        auto cur = _q.front();
        _q.pop();
        return cur;
    }

    std::coroutine_handle<> await_suspend_impl(std::coroutine_handle<> h) {
        _q.push(h);
        return await_suspend_impl();
    }

    void resume_impl(std::coroutine_handle<> h) noexcept {
        if (_in_coroutine) {
            _q.push(h);
            return;
        }
        _in_coroutine = true;
        h.resume();
        while (!_q.empty()) {
            h = _q.front();
            _q.pop();         
            h.resume();
        }
        _in_coroutine = false;
    }


protected:
    std::queue<std::coroutine_handle<> > _q;
    bool _in_coroutine = false;
};

class pause_awaiter {
public:
    static bool await_ready() noexcept {return false;}
    static std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept{
        return resume_ctl::await_suspend(h);
    }
    static void await_resume() noexcept {}
};


template<typename Awt>
class resume_ctl_awaiter {
public:
    resume_ctl_awaiter(Awt &&awt):_awt(std::forward<Awt>(awt)) {}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        return resume_ctl::await_suspend_awt(std::forward<Awt>(_awt),h);
    }
    bool await_ready() noexcept {
        return _awt.await_ready();
    }
    auto await_resume() {
        return _awt.await_resume();
    }
    Awt _awt;
};

template<typename Awt>
class resume_ctl_awaiter<const Awt> {
public:
    resume_ctl_awaiter(const Awt &&awt):_awt(std::forward<const Awt>(awt)) {}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) const noexcept {
        return resume_ctl::await_suspend_awt(std::forward<const Awt>(_awt),h);
    }
    bool await_ready() const noexcept {
        return _awt.await_ready();
    }
    auto await_resume() const  {
        return _awt.await_resume();
    }
    const Awt _awt;
};



///Pause current coroutine in favor to other coroutine ready to run
/**
 * For example if you want to start a coroutine in other coroutine without waiting for its result, but you
 * need to be ensured, that coroutine stopped on the first co_await, you can co_await pause()
 *
 * @code
 * task<> t2 = run_task2(args); //<t2 starts suspended
 * co_await pause();            //give to t2 chance to run to reach first co_await
 * do_something_other();
 * co_await t2;                 //finally join with t2
 * @return
 */
inline pause_awaiter pause() {
    return {};
}

template<typename Fn>
class manual_resume_awaiter {
public:
    manual_resume_awaiter(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
    static bool await_ready() noexcept {return false;}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        if constexpr(std::is_convertible_v<decltype(_fn(h)), std::coroutine_handle<> >) {
            return _fn(h);
        } else {
            _fn(h);
            return resume_ctl::await_suspend();
        }
    }
    static void await_resume() {}

protected:
    Fn _fn;
};


///Suspend current coroutine and forward its handle to somewhere
/**
 * @code
 * co_await suspend([](std::coroutine_handle<>){...})
 * @endcode
 *
 * Function suspend current coroutine in exchange to retrieve its handle and calls
 * a function with that handle. It can be useful to register this coroutine for a signal
 * @param fn function to be called with handle

 * @return awaiter
 */
template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<std::coroutine_handle<> >()))>
manual_resume_awaiter<Fn> suspend(Fn &&fn) {
    return manual_resume_awaiter<Fn>(std::forward<Fn>(fn));
}


}



#endif /* SRC_COCLASSES_RESUME_CTL_H_ */
