#pragma once
#ifndef SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_
#define SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_

#include "trailer.h"

#include "resumption_policy.h"

#include <coroutine>
#include <queue>
#include <utility>

namespace cocls {





namespace resumption_policy {

///Coroutines are scheduled using queue which is managed in current thread
/**
 * The queue is initialized when the first coroutine is called and the function
 * doesn't return until the queue is empty. The queue is active only if there are
 * running coroutines. When the last coroutine finishes, the queue is destroyed (and
 * can be reinstalled again)
 *
 * Coroutines are started immediately (no suspension at the beginning), however the first
 * coroutine performs temporary suspend needed to initialize the queue. Further coroutines
 * are started with no temporary suspension
 *
 * When coroutine is being resumed, its handle is placed to the queue and it is resumed once
 * the current coroutine is finished or suspended.
 *
 * When a coroutine is finished, it performs symmetric transfer to the coroutine which is
 * waiting on result. This literally skips the queue in favor awaiting coroutine.
 */
struct queued {

    struct queue_impl {

        queue_impl() = default;
        queue_impl(const queued &) = delete;
        queue_impl&operator=(const queued &) = delete;
        std::queue<std::coroutine_handle<> > _queue;

        void flush_queue() noexcept {
            while (!_queue.empty()) {
                _queue.front().resume();
                _queue.pop();
            }

        }

        void push(std::coroutine_handle<> h) noexcept {
            _queue.push(h);
        }
    };

    static bool is_active() {
        return instance != nullptr;
    }

    ///Installs queue and resume coroutine
    /**
     * Function always install a new queue, even if there is an active queue.
     * This nested queue is cleared before the function returns. Coroutines
     * enqueued to previous queue are not schedulued untile the nested queue is
     * flushed()
     *
     * @param h coroutine to resume after queue is installed
     */
    static void install_queue_and_resume(std::coroutine_handle<> h) {
        queue_impl q;
        auto prev = std::exchange(instance , &q);
        h.resume();
        q.flush_queue();
        instance = prev;
    }

    ///Installs coroutine queue and calls the function
    /**
     * This is useful,if you need to ensure, that coroutines - that
     * with resumption policy: queued, are not resumed immediately, because
     * no queue has been installed yet. The function installs the queue and calls
     * the function. Extra arguments are passed to the function. Return value of
     * the function is returned. Queue is uninstalled upon return resuming all
     * queued coroutines at this point.
     *
     * @note if there is already active queue, a new nested queue is installed and
     * scheduled coroutines are placed here. They are resumed and processed before
     * the function returns.
     *
     * @param fn function to call
     * @param args arguments passed to function
     * @return
     */
    template<typename Fn, typename ... Args>
    CXX20_REQUIRES(std::invocable<Fn, Args...> )
    static auto install_queue_and_call(Fn &&fn, Args &&... args) {
        queue_impl q;
        auto prev = std::exchange(instance, &q);
        auto x = trailer([&]{
            q.flush_queue();
            instance = prev;
        });
        return fn(std::forward<Args>(args)...);

    }

    ///resume in queue
    static void resume(std::coroutine_handle<> h) noexcept {

        if (instance) {
            instance->push(h);
        } else {
            install_queue_and_resume(h);
        }
    }

    struct initial_awaiter {
        //initial awaiter is called with instance, however this instance is not used here
        //because the object is always empty
        initial_awaiter(queued &) {}
        //coroutine don't need to be temporary suspended if there is an instance of the queue
        static bool await_ready() noexcept {return is_active();}
        //we need to install queue before the coroutine can run
        //this is the optimal place
        //function installs queue and resumes under the queue
        static void await_suspend(std::coroutine_handle<> h) noexcept {
            install_queue_and_resume(h);
        }
        //always empty
        static constexpr void await_resume() noexcept {}
    };

    static constexpr auto resume_handle(std::coroutine_handle<> h) {
        return h;
    }

    static thread_local queue_impl *instance;
};

inline thread_local queued::queue_impl *queued::instance = nullptr;


}


}



#endif /* SRC_COCLASSES_QUEUED_RESUMPTION_POLICY_H_ */
