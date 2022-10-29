#pragma once
#ifndef SRC_COCLASSES_TASK_STORAGE_H_
#define SRC_COCLASSES_TASK_STORAGE_H_

#include "task.h"

#include <vector>

namespace cocls {

///Reusable storage for tasks.
/** Pass reference to this storage as first argument of coroutine, and the task will be allocated using this storage
 * 
 * @note The storage will not receive notification about deallocation. You
 * need to destroy storage manually. Because it is an object, it would be easy
 * to handle the lifetime of the object
 * 
 * @code 
 * task<int> example(reusable_task_storage &, int args) {
 *  co_return args;
 * }
 * 
 * {
 *    reusable_task_storage storage;
 *    int res = example(storage, 42).join();
 *    //...
 * }
 * @endcode
 * 
 */
class reusable_task_storage: public task_storage {
public:
    void *alloc(std::size_t sz) override {
        _storage.resize(sz);
        return _storage.data();
    }
    std::size_t capacity() const override {return _storage.size();}
    
protected:
    std::vector<char> _storage;
};

///Similar to placement new for coroutines
/**
 * Initalize with pointer to a memory where the coroutine will be allocated
 * 
 * @code 
 * task<int> example(placement_alloc &, int args)
 *      co_return args;
 * }
 * 
 * 
 * {
 *    char buff[1000];
 *    placement_alloc storage(buff);
 *    int res = example(storage, 42).join();
 *    //...
 * }
 * @endcode
  
 *  
 */
class placement_alloc: public task_storage {
public:
    placement_alloc(void *p):_p(p) {}
    virtual void *alloc(std::size_t) override { return _p;}
    virtual std::size_t capacity() const override {return -1;}
protected:
    void *_p;

};


}

#endif /* SRC_COCLASSES_TASK_STORAGE_H_ */
