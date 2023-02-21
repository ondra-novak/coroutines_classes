#pragma once
#ifndef SRC_COCLASSES_SRC_COCLASSES_WITH_ALLOCATOR_H_
#define SRC_COCLASSES_SRC_COCLASSES_WITH_ALLOCATOR_H_


#include "common.h"
#include <coroutine>
#include <concepts>


namespace cocls {


template<typename Allocator, typename Base> CXX20_REQUIRES(Storage<Allocator>)
class custom_allocator_base: public Base {
public:

    using Base::Base;

    template<typename ... Args>
    void *operator new(std::size_t sz, Allocator &storage, Args && ... ) {
        return storage.alloc(sz);
    }

    template<typename This, typename ... Args>
    void *operator new(std::size_t sz, This &, Allocator &storage, Args && ... ) {
        return storage.alloc(sz);
    }

    void operator delete(void *ptr, std::size_t sz) {
        Allocator::dealloc(ptr, sz);
    }


private:
    void *operator new(std::size_t); //incorrectly use of with_allocator


};

///declares coroutine which frame is allocated through the allocator
/**
 * @tparam Task original coroutine class
 * @tparam Allocator allocator
 *
 * @code
 * with_allocator<task<int>, my_allocator> coro() {
 *
 * }
 * @endcode
 *
 * Because the class inherits the original Task, you should be
 * able to convert returned instance to original Task instance.
 *
 * The allocator needs to contains following functions
 * @code
 * void *alloc(std::size_t sz);
 * void dealloc(void *ptr, std::size_t sz);
 * std::size_t get_extra_space();
 * void store_instance(void *ptr)
 * static auto restore_instance(void *ptr);
 * @endcode
 *
 * @see coro_storage
 *
 */
template<typename Allocator, typename Task> CXX20_REQUIRES(Storage<Allocator>)
class with_allocator: public Task {
public:

    using Task::Task;
    with_allocator(Task &&arg):Task(std::move(arg)) {}

    using promise_type = custom_allocator_base<Allocator, typename Task::promise_type>;


};


}



#endif /* SRC_COCLASSES_SRC_COCLASSES_WITH_ALLOCATOR_H_ */
