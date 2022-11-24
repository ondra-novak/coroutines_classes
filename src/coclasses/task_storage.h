#pragma once
#ifndef SRC_COCLASSES_TASK_STORAGE_H_
#define SRC_COCLASSES_TASK_STORAGE_H_

#include "task.h"

#include <vector>

namespace cocls {

///Represents preallocated space for the task
/**
 * @tparam space preallocated spaces
 * 
 * Pass reference of this object as the first argument of a coroutine
 * 
 * @code 
 * task<int> example(static_task_storage<1000> &, int args)
 *      co_return args;
 * }
 * 
 * 
 * {
 *    static_task_storage<1000> storage;
 *    int res = example(storage, 42).join();
 *    //...
 * }
 * @endcode
 */
template<std::size_t space>
class static_task_storage:public task_storage {
public:
#ifdef _WIN32 //msvc coroutines are much larger
    static constexpr std::size_t multiplier = 250;
#else
    static constexpr std::size_t multiplier = 100;
#endif
    static constexpr std::size_t adjspace = space * multiplier /100;

    virtual void *alloc(std::size_t sz) override { 
        assert(sz <= adjspace); //space is too small to fit the cooroutine frame;
        return _buffer;
    }
    virtual void dealloc(void *, std::size_t) override {}
    virtual void dealloc_on_exception(void *) override {}
    virtual std::size_t capacity() const override {return space;}
protected:
    char _buffer[adjspace];
};

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
    virtual void dealloc(void *, std::size_t) override {}
    virtual void dealloc_on_exception(void *) override {}

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
    virtual void dealloc(void *, std::size_t) override{}
    virtual void dealloc_on_exception(void *) override{}

protected:
    void *_p;

};


}

#endif /* SRC_COCLASSES_TASK_STORAGE_H_ */
