#pragma once
#ifndef SRC_COCLASSES_SRC_COCLASSES_WITH_ALLOCATOR_H_
#define SRC_COCLASSES_SRC_COCLASSES_WITH_ALLOCATOR_H_


#include "common.h"
#include <coroutine>


namespace cocls {



///Abstract class to define storage for coroutines
class coro_storage {
public:
    coro_storage() = default;
    coro_storage(coro_storage &) = delete;
    coro_storage &operator=(coro_storage &) = delete;
    virtual ~coro_storage()=default;

    ///allocate space for the coroutine
    /**
     * @param sz required size
     */
    virtual void *alloc(std::size_t sz) = 0;
    ///deallocate the space
    /**
     * @param ptr pointer to memory
     * @param sz size of memory
     */
    virtual void dealloc(void *ptr, std::size_t sz) = 0;

    ///Retrieves extra space needed to store reference to the instance
    /** The reference is stored after the coroutine frame, and it is used to restore
     * allocator instance during deallocation
     * @return required size
     */
    static constexpr std::size_t get_extra_space() {return sizeof(coro_storage *);}

    ///Stores instance of this allocator to the target memory
    /**
     * @param ptr pointer to memory contains allocated space where instance can be stored
     */
    void store_instance(void *ptr) {
        auto s = reinterpret_cast<coro_storage **>(ptr);
        *s = this;
    }

    ///Restores instance from the memory
    /**
     * Pointer to the memory, where instance has been previously stored.
     * @param ptr
     * @return pointer to the instance.
     *
     * @note function can also destroy the object at the address (not the allocator
     * itself, just the reference) because it is being deallocated. This is needed
     * especially when reference is stored as a smart pointer.
     */
    static coro_storage *restore_instance(void *ptr) {
        auto s = reinterpret_cast<coro_storage **>(ptr);
        return *s;
    }

};


using default_allocator = coro_storage;

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
template<typename Task, typename Allocator = default_allocator>
class with_allocator: public Task {
public:

    using Task::Task;
    with_allocator(Task &&arg):Task(std::move(arg)) {}

    class promise_type: public Task::promise_type {
    public:

        template<typename ... Args>
        void *operator new(std::size_t sz, Allocator &storage, Args && ... ) {
            void *p = storage.alloc(sz+Allocator::get_extra_space());
            storage.store_instance(reinterpret_cast<std::byte *>(p)+sz);
            return p;
        }

        template<typename This, typename ... Args>
        void *operator new(std::size_t sz, This &, Allocator &storage, Args && ... ) {
            void *p = storage.alloc(sz+Allocator::get_extra_space());
            storage.store_instance(reinterpret_cast<std::byte *>(p)+sz);
            return p;
        }

        void operator delete(void *ptr, std::size_t sz) {
            auto inst = Allocator::restore_instance(reinterpret_cast<std::byte *>(ptr)+sz);
            inst->dealloc(ptr, sz+Allocator::get_extra_space());
        }


    private:
        void *operator new(std::size_t); //incorrectly use of with_allocator

    };

};


}



#endif /* SRC_COCLASSES_SRC_COCLASSES_WITH_ALLOCATOR_H_ */
