/** @file generator_aggregator.h */
#ifndef SRC_COCLASSES_GENERATOR_AGGREGATOR_H_
#define SRC_COCLASSES_GENERATOR_AGGREGATOR_H_
#include "generator.h"
#include "queue.h"
#include "detached.h"

namespace cocls {

namespace _details {

template<typename T, typename Arg>
class GenCallback;

template<typename T, typename Arg>
using GenAggrQueue = queue<GenCallback<T, Arg> *,primitives::std_queue, primitives::single_item_queue>;

template<typename T, typename Arg>
class GenCallback: public abstract_awaiter {
public:
    GenCallback(GenAggrQueue<T, Arg> &q,generator<T, Arg> gen):_q(q), _gen(std::move(gen)) {}
    GenCallback(const GenCallback &) =delete;
    GenCallback(GenCallback &&) =default;
    GenCallback &operator=(const GenCallback &) = delete;

    virtual void resume() noexcept override  {
        _q.push(this);
    }
    virtual std::coroutine_handle<> resume_handle() noexcept override  {
        GenCallback<T, Arg>::resume();
        return std::noop_coroutine();
    }
    template<typename ... Args>
    void charge(Args && ... args) {
        _gen.next(std::forward<Args>(args)...).subscribe_awaiter(this);
    }
    generator<T, Arg> &get_generator() {
        return _gen;
    }
protected:
    GenAggrQueue<T, Arg> &_q;
    generator<T, Arg> _gen;
};


//tracks count of active generators, handles final destruction
/* Because generators can be async and during destruction, they can
 * be still pending. So before we can destroy them we must
 * wait for finish. This is blocking operation!
 *
 * cleanup is easy, just flush whole queue synchronously
 */
template<typename T, typename Arg>
struct generator_aggregator_controller {
    generator_aggregator_controller(std::size_t count, GenAggrQueue<T, Arg> &queue):_count(count),_queue(queue) {}
    generator_aggregator_controller(const generator_aggregator_controller  &) = delete;
    generator_aggregator_controller &operator=(const generator_aggregator_controller  &) = delete;
    ~generator_aggregator_controller() {
        //there is always 1 less value, than count active of generators
        //because only place when you can delete generator_aggregator
        //is during co_yield. And because we are in co_yield, one
        //of generators is not active
        while (_count>1) {
            _queue.pop().wait();
            _count--;
        }
    }
    operator bool() const {return _count>0;}
    void fin() {_count--;}

    std::size_t _count;
    GenAggrQueue<T, Arg> &_queue;
};


}
///Aggregator of multiple generators
/**
 * @param list__ list of generators to aggregate. The vector is passed
 * as rvalue reference to avoid copying (because generators are movable)_
 * @return generator
 *
 * @note for generator with an argument, the aggregator works differently. The first call
 * of such generator initializes all aggregated generators with the value you passed as argument
 * All other calls passes value to generator which result has been returned previously.
 */

template<typename T, typename Arg>
generator<T, Arg> generator_aggregator(std::vector<generator<T, Arg> > list__) {

    std::exception_ptr exp;

    using Queue = _details::GenAggrQueue<T, Arg>;
    using controller = _details::generator_aggregator_controller<T, Arg>;

    std::vector<_details::GenCallback<T, Arg> > cbs;
    cbs.reserve(list__.size());
    Queue queue;
    controller cnt(list__.size(), queue);


    if constexpr(std::is_void_v<Arg>) {
        for (auto &x: list__) {
            cbs.emplace_back(queue, std::move(x));
            cbs.back().charge();
        }
    } else {
        auto arg = co_yield nullptr;
        for (auto &x: list__) {
            cbs.emplace_back(queue, std::move(x));
            cbs.back().charge(arg);
        }
    }
    while (cnt) {
        _details::GenCallback<T, Arg> *gcb = co_await queue.pop();
        auto &g = gcb->get_generator();
        if (g.done()) {
            cnt.fin();
        } else {
            try {
                if constexpr(std::is_void_v<Arg>) {
                    co_yield g.value();
                    gcb->charge();
                } else {
                    auto arg = co_yield g.value();
                    gcb->charge(arg);
                }
            } catch (...) {
                exp = std::current_exception();
                cnt.fin();
            }

        }
    }
    if (exp) std::rethrow_exception(exp);

}


}
#endif /* SRC_COCLASSES_GENERATOR_AGGREGATOR_H_ */
