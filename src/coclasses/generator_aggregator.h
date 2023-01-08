/** @file generator_aggregator.h */
#ifndef SRC_COCLASSES_GENERATOR_AGGREGATOR_H_
#define SRC_COCLASSES_GENERATOR_AGGREGATOR_H_
#include "generator.h"
#include "limited_queue.h"

namespace cocls {

namespace _details {


//tracks count of active generators, handles final destruction
/* Because generators can be async and during destruction, they can
 * be still pending. So before we can destroy them we must
 * wait for finish. This is blocking operation!
 * 
 * cleanup is easy, just flush whole queue synchronously
 */
template<typename T>
struct generator_aggregator_controller {
    using Queue = limited_queue<generator<T> *, primitives::single_item_queue<abstract_awaiter<> * > >;
    generator_aggregator_controller(std::size_t count, Queue &queue):_count(count),_queue(queue) {}
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
    Queue &_queue;
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
    
    using Queue = limited_queue<generator<T> *, primitives::single_item_queue<abstract_awaiter<> * > >; 
    using controller = _details::generator_aggregator_controller<T>;
    
    
    std::vector<generator<T> > list(std::move(list__));
    Queue queue(list.size());
    controller cnt(list.size(), queue);
    
    for (auto &x: list) {
        x >> [&x,&queue]{
            queue.push(&x);
        };
    }
    while (cnt) {
        generator<T> *g = co_await queue.pop();
        if (g->done()) {
            cnt.fin();
        } else {
            try {
                co_yield g->value();
                (*g) >> [g,&queue]{
                    queue.push(g);
                };
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
