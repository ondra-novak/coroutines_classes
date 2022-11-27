/**
 * @file coro_storage.h 
 * 
 * various storages for co_new() 
 * 
 * @see co_new.h
 */

#pragma once
#ifndef SRC_COCLASSES_CORO_STORAGE_H_
#define SRC_COCLASSES_CORO_STORAGE_H_

#include "task.h"

#include <vector>

namespace cocls {


///Defines multiplier for all statically allocated storages for coroutines
/**
 * In current C++ version (C++20) it is very difficult to determine space needed to
 * coroutine frame. The value must be determined by guessing, trial and error. The
 * final value can be valid for some compilers. For other compilers, the value can
 * be insufficient which results to assert (in debug) or not using static storage at all. 
 * If this happen, you can redefine COCLS_STATIC_STORAGE_MULTIPLIER and specify how much this
 * number must be increased globally. The value is in percent, so setting 150 means, that all
 * sizes are multiplied by 1.5 times.
 * 
 * This constant can be passed at command line as -DCOCLS_STATIC_STORAGE_MULTIPLIER=150 
 */ 
#ifndef COCLS_STATIC_STORAGE_MULTIPLIER
#ifdef _WIN32
#define COCLS_STATIC_STORAGE_MULTIPLIER 250
#else
#define COCLS_STATIC_STORAGE_MULTIPLIER 100
#endif
#endif

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
class static_storage:public coro_storage {
public:
    static constexpr std::size_t multiplier = COCLS_STATIC_STORAGE_MULTIPLIER;
    static constexpr std::size_t adjspace = space * multiplier /100;

    virtual void *alloc(std::size_t sz) override { 
        assert(sz <= adjspace); //space is too small to fit the cooroutine frame;
        if (sz > adjspace) return ::operator new(sz);
        return _buffer;
    }
    virtual void dealloc(void *ptr, std::size_t sz) override {
        if (sz > adjspace) return ::operator delete(ptr);        
    }
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
class reusable_storage: public coro_storage {
public:
    void *alloc(std::size_t sz) override {
        _storage.resize(sz);
        return _storage.data();
    }
    virtual void dealloc(void *, std::size_t) override {}
    
    std::size_t capacity() const {return _storage.size();}

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
class placement_alloc: public coro_storage {
public:
    placement_alloc(void *p):_p(p) {}
    virtual void *alloc(std::size_t) override { return _p;}
    virtual void dealloc(void *, std::size_t) override{}

protected:
    void *_p;

};


}

#endif /* SRC_COCLASSES_CORO_STORAGE_H_ */
