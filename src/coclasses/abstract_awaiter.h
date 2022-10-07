/*
 * abstract_awaiter.h
 *
 *  Created on: 7. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_ABSTRACT_AWAITER_H_
#define SRC_COCLASSES_ABSTRACT_AWAITER_H_


#include <algorithm>
#include "handle.h"

namespace cocls {

///Abstract interface for awaiter
/**
 * Useful to transfer waitable operation outside of function, which need to perform operation.
 * @tparam T
 */
template<typename T>
class abstract_awaiter {
public:
    virtual bool await_ready() = 0;
    virtual std::coroutine_handle<> await_suspend(handle_t h) = 0;
    virtual T await_resume() = 0;
    virtual ~abstract_awaiter() = default;
};


template<typename T, typename Target>
class awaiter_proxy: public abstract_awaiter<T> {
public:
    awaiter_proxy(Target t):_target(std::move(t)) {}
    virtual bool await_ready() override {
        return _target.await_ready();
    }
    virtual std::coroutine_handle<> await_suspend(handle_t h) override {
        return _target.await_suspend(h);
    }
    virtual T await_resume() override {
        return _target.await_resume();
    }
    
    
protected:
    Target _target;
};

}



#endif /* SRC_COCLASSES_ABSTRACT_AWAITER_H_ */
