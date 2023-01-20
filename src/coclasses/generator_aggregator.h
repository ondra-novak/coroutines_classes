/** @file generator_aggregator.h */
#ifndef SRC_COCLASSES_GENERATOR_AGGREGATOR_H_
#define SRC_COCLASSES_GENERATOR_AGGREGATOR_H_
#include "generator.h"
#include "limited_queue.h"
#include "detached.h"

namespace cocls {

namespace _details {

template<typename T>
class GenCallback;

template<typename T>
using GenAggrQueue = limited_queue<GenCallback<T> *, primitives::single_item_queue<abstract_awaiter<> * > >; 

template<typename T>
class GenCallback: public abstract_awaiter<false> {
public:
    GenCallback(GenAggrQueue<T> &q,generator<T> gen):_q(q), _gen(std::move(gen)) {}
    GenCallback(const GenCallback &) =delete;
    GenCallback(GenCallback &&) =default;
    GenCallback &operator=(const GenCallback &) = delete;
    
    virtual void resume() noexcept override  {
        _q.push(this);
    }        
    virtual std::coroutine_handle<> resume_handle() noexcept override  {
        GenCallback<T>::resume();
        return std::noop_coroutine();
    }
    void charge() {
        _gen.next().subscribe_awaiter(this);
    }
    generator<T> &get_generator() {
        return _gen;
    }
protected:
    GenAggrQueue<T> &_q;
    generator<T> _gen;
};


//tracks count of active generators, handles final destruction
/* Because generators can be async and during destruction, they can
 * be still pending. So before we can destroy them we must
 * wait for finish. This is blocking operation!
 * 
 * cleanup is easy, just flush whole queue synchronously
 */
template<typename T>
struct generator_aggregator_controller {
    generator_aggregator_controller(std::size_t count, GenAggrQueue<T> &queue):_count(count),_queue(queue) {}
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
    GenAggrQueue<T> &_queue;
};


}
///Aggregator of multiple generators
/**
 * @param list__ list of generators to aggregate. The vector is passed 
 * as rvalue reference to avoid copying (because generators are movable)_
 * @return generator
 */

template<typename T>
generator<T> generator_aggregator(std::vector<generator<T> > list__) {
    
    
    
    std::exception_ptr exp;
    
    using Queue = _details::GenAggrQueue<T>; 
    using controller = _details::generator_aggregator_controller<T>;

    std::vector<_details::GenCallback<T> > cbs;
    cbs.reserve(list__.size());
    Queue queue(list__.size());
    controller cnt(list__.size(), queue);
    
    
    for (auto &x: list__) {
        cbs.emplace_back(queue, std::move(x));
        cbs.back().charge();
    }
    while (cnt) {
        _details::GenCallback<T> *gcb = co_await queue.pop();
        auto &g = gcb->get_generator();
        if (g.done()) {
            cnt.fin();
        } else {
            try {
                co_yield g.value();
                gcb->charge();
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
