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
#include <cassert>

#include "with_allocator.h"

#include <utility>
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



///Reusable storage for tasks.

class reusable_storage {
public:

    reusable_storage() = default;
    reusable_storage(const reusable_storage &other) = delete;
    reusable_storage &operator=(const reusable_storage &other) = delete;
    reusable_storage(reusable_storage &&other)
        :_ptr(std::exchange(other._ptr,nullptr))
        ,_capacity(std::exchange(other._capacity, 0)) {}
    reusable_storage &operator=(reusable_storage &&other) {
        if (this != &other) {
            ::operator delete (_ptr);
            _ptr = std::exchange(other._ptr,nullptr);
            _capacity = std::exchange(other._capacity, 0);
        }
        return *this;
    }

    ~reusable_storage() {
        ::operator delete (_ptr);
    }
    void *alloc(std::size_t sz) {
        if (sz > _capacity) {
            ::operator delete (_ptr);
            _ptr = ::operator new(sz);
            _capacity = sz;
        }
        return _ptr;
    }
    static constexpr void dealloc(void *, std::size_t) {}

    std::size_t capacity() const {return _capacity;}

protected:
    void *_ptr = nullptr;
    std::size_t _capacity = 0;
};


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
class static_storage {
public:
    static constexpr std::size_t multiplier = COCLS_STATIC_STORAGE_MULTIPLIER;
    static constexpr std::size_t adjspace = space * multiplier /100;

    void *alloc(std::size_t sz)  {
        auto exsz = sz+sizeof(static_storage **);
        assert(exsz <= adjspace); //space is too small to fit the cooroutine frame;
        void *p;
        if (exsz > adjspace) p = ::operator new(exsz);
        else p = _buffer;
        auto s = reinterpret_cast<static_storage **>(reinterpret_cast<char *>(p) + sz);
        *s = this;
        return p;
    }
    void dealloc(void *ptr, std::size_t sz)  {

        auto s = reinterpret_cast<static_storage **>(reinterpret_cast<char *>(ptr) + sz);
        auto me = *s;

        if (ptr != _buffer) return ::operator delete(ptr);
    }
protected:
    char _buffer[adjspace];
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
class placement_alloc {
public:
    placement_alloc(void *p):_p(p) {}
    void *alloc(std::size_t) noexcept { return _p;}
    static void dealloc(void *, std::size_t) {}


protected:
    void *_p;

};


///Reusable storage with MT usage protection
/**
 * Purpose of this storage is to be used with classes that working heavily with coroutines where
 * is can happen, that coroutine can be called from multiple threads but it is mostly unlikely.
 * When the storage is used again while there is already running coroutine, the other
 * allocations is handled by standard allocator. This is the way how the storage is protected.
 */
class reusable_storage_mtsafe: public reusable_storage {
public:
    void *alloc(std::size_t sz)  {
        void *p;
        if (_busy.exchange(true, std::memory_order_relaxed)) {
            p = coro_promise_base::default_new(sz+sizeof(reusable_storage_mtsafe **));
        } else {
            p = reusable_storage::alloc(sz+sizeof(reusable_storage_mtsafe **));
        }
        auto s = reinterpret_cast<reusable_storage_mtsafe **>(reinterpret_cast<char *>(p) + sz);
        *s = this;
        return p;
    }
    static void dealloc(void *ptr, std::size_t sz) {
        auto s = reinterpret_cast<reusable_storage_mtsafe **>(reinterpret_cast<char *>(ptr) + sz);
        auto me = *s;
        if (ptr == me->_ptr) {
            me->_busy.store(false, std::memory_order_relaxed);
        } else {
            coro_promise_base::default_delete(ptr, sz);
        }
    }
protected:
    std::atomic<bool> _busy = {false};
};


///Converts any buffer with POD (Plain Old Data) to act as temporary buffer for coroutine
/**
 * During working with data and coroutines, there can be a lot unused buffers with preallocated
 * storage. Such a buffer can be used as storage for a coroutine frame. You only need to
 * ensure, that buffer is not used while the coroutine is active. Keep in mind, that content of
 * buffer is destroyed (actually, it destroys begin of the buffer. If the coroutine is
 * called repeatedly, it  destroys only begin of the buffer of the same size on each call,
 * so you can use rest of the buffer for any arbitrary data)
 *
 * @tparam Buffer type
 */
template<typename Buffer>
class reusable_buffer_storage {
public:
    ///Construct the storage, pass reference to unused buffer to it
    reusable_buffer_storage(Buffer &buff):_buff(buff) {
        static_assert(std::is_trivial_v<std::remove_reference_t<decltype(*buff.data())> >, "Only buffer of POD data can be used as temporary storage");
    }
    void *alloc(std::size_t sz) {
        constexpr std::size_t itemsz = sizeof(decltype(*(this->_buff.data())));
        std::size_t items = (sz+itemsz-1)/itemsz;
        if (_buff.size() < items) _buff.resize(items);
        return _buff.data();
    }
    static constexpr void dealloc(void *, std::size_t) {}

protected:
    Buffer &_buff;
};



}

#endif /* SRC_COCLASSES_CORO_STORAGE_H_ */
