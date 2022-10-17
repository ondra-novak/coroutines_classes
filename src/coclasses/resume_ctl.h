#ifndef SRC_COCLASSES_RESUME_CTL_H_
#define SRC_COCLASSES_RESUME_CTL_H_

#include <coroutine>
#include <queue>

namespace cocls {


class resume_ctl {
public:

    static std::coroutine_handle<> await_suspend() {
        return instance().await_suspend_impl();
    }
    static std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        return instance().await_suspend_impl(h);
    }
    static void resume(std::coroutine_handle<> h) {
        return instance().resume_impl(h);
    }
    static bool start_coroutine(std::coroutine_handle<> h) {
        return instance().start_coroutine_impl(h);
    }

protected:
    static resume_ctl &instance() {
        static thread_local resume_ctl inst;
        return inst;
    }

    std::coroutine_handle<> await_suspend_impl() {
        if (_q.empty()) {
            _cur = nullptr;
            return std::noop_coroutine();
        }

        _cur = _q.front();
        _q.pop();
        return _cur;
    }

    std::coroutine_handle<> await_suspend_impl(std::coroutine_handle<> h) {
        _q.push(h);
        return await_suspend_impl();
    }

    void resume_impl(std::coroutine_handle<> h) {
        if (_cur) {
            _q.push(h);
            return;
        }
        _cur = h;
        h.resume();
        while (!_q.empty()) {
            h = _q.front();
            _q.pop();
            _cur = h;
            h.resume();
        }
        _cur = nullptr;
    }

    bool start_coroutine_impl(std::coroutine_handle<> h) {
        if (_cur == nullptr) {
            _cur = h;
            return false;
        } else {
            resume_impl(h);
            return true;
        }
    }

protected:
    std::queue<std::coroutine_handle<> > _q;
    std::coroutine_handle<> _cur;
};

class task_initial_suspender {
public:
    static bool await_ready() noexcept {return false;}
    static bool await_suspend(std::coroutine_handle<> h) noexcept{
        return resume_ctl::start_coroutine(h);
    }
    static void await_resume() noexcept {}
};


class task_final_suspender {
public:
    task_final_suspender(bool destroy):_destroy(destroy) {}
    static bool await_ready() noexcept {return false;}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        if (_destroy) h.destroy();
        return resume_ctl::await_suspend();
    }
    static void await_resume() noexcept {}

protected:
    bool _destroy;
};

class pause_awaiter {
public:
    static bool await_ready() noexcept {return false;}
    static std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept{
        return resume_ctl::await_suspend(h);
    }
    static void await_resume() noexcept {}
};

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
