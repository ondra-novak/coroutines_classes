/*
 * reusable.h
 *
 *  Created on: 8. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_REUSABLE_H_
#define SRC_COCLASSES_REUSABLE_H_
#include "task.h"
#include "debug.h"

#include <array>
#include <vector>
#include <new>

#ifndef NDEBUG
#include <iostream>
#endif

namespace cocls {


class abstract_reusable_memory {
public:
    virtual void *alloc(std::size_t) = 0;
    virtual void dealloc(void *ptr) = 0;
    virtual ~abstract_reusable_memory() = default;
};


///Reusable buffer for reusable coroutine
/**
 * @see reusable
 */
template<typename Storage = std::string>
class reusable_memory;

template<>
class reusable_memory<void>:abstract_reusable_memory {
public:
    static std::size_t adjust_size(std::size_t sz) {
        return sz + sizeof(abstract_reusable_memory *);        
    }
    
    static void *finish_alloc(void *p, abstract_reusable_memory *owner) {
        abstract_reusable_memory **s= reinterpret_cast<abstract_reusable_memory **>(p);
        *s = owner;
        return s+1;
        
    }
    
    static abstract_reusable_memory **get_block_ptr(void *p) {
        return reinterpret_cast<abstract_reusable_memory **>(p) - 1;        
    }
    
    void *alloc(std::size_t sz) {
        return finish_alloc(::operator new(adjust_size(sz)), this);
    }
    
    void dealloc(void *ptr) {
        ::operator delete(ptr); 
    }
    static void generic_delete(void *ptr) {
        abstract_reusable_memory **p = get_block_ptr(ptr);
        (*p)->dealloc(p);
    }
    static reusable_memory &get_instance() {
        static reusable_memory x;
        return x;
    }
    
};


template<typename Storage>
class reusable_memory: public abstract_reusable_memory {
public:
    

    ///Allocate block
    /**
     * This object can hold only one block. If block is already allocated, standard new is used
     * 
     * @note not MT Safe
     * @param sz requested size
     */
    void *alloc(std::size_t sz) {
        bool r = _busy.exchange(true);
        if (r) {
            return reusable_memory<void>::get_instance().alloc(sz);
        } else {
            auto nsz = reusable_memory<void>::adjust_size(sz);
            _buffer.resize(nsz);
            return reusable_memory<void>::finish_alloc(_buffer.data(), this);
        }
    }
    ///Deallocate block allocated by alloc
    /**
     * 
     * @param ptr pointer to block returned by alloc()
     */
    void dealloc(void *) {
        _busy = false;
    }
    
    
protected:
    std::atomic<bool> _busy = false;
    Storage _buffer;
        
};


template<typename T, std::size_t n>
class reusable_memory<std::array<T,n> >:public abstract_reusable_memory {
public:
    void *alloc(std::size_t sz) {
        auto nsz = reusable_memory<void>::adjust_size(sz);
#ifndef NDEBUG
        if (nsz > sizeof(T)*n) {
            std::cerr << "Failed to allocate coroutine. Reserved space is too small. " << ((nsz+sizeof(T)-1)/sizeof(T)) << ">" << n << std::endl;
        }
#endif
        if (nsz > sizeof(T)*n) {
            throw std::bad_alloc();
        }
        return reusable_memory<void>::finish_alloc(_storage.data(), this);        
    }
    
    void dealloc(void *) {
        //empty
    }
private:
    std::array<T,n> _storage;
};



///A coroutine which reuses already allocated memory.
/**
 * @tparam Coro coroutine future type  (example: reusable<task<int> >)
 * 
 * The coroutine needs an instance of reusable_memory as the first argument.  
 * 
 * @code
 * reusable<task<int> > test_reusable(reusable_memory &) {
 *      co_return 42;
 * }
 * 
 * 
 */
template<typename Coro, typename Storage>
class reusable: public Coro {
public:
    
    reusable(Coro &&x):Coro(std::move(x)) {}
    reusable() {}
    
    class promise_type: public Coro::promise_type {
    public:
        
#ifndef NDEBUG
        template<typename T, typename ... Args>
        static void arg_types(std::vector<const std::type_info *> &targs, T&& , Args &&...args) {
            targs.push_back(&typeid(T));
            arg_types(targs, std::forward<Args>(args)...);
        }

        static void arg_types(std::vector<const std::type_info *> &targs) {
        }
#endif

        template<typename ... Args>
        static void report_size(std::size_t sz, Args && ... args) {
#ifndef NDEBUG
            static bool rep = true;
            if (rep) {
                rep = false;
                std::vector<const std::type_info *> targs;
                arg_types(targs, std::forward<Args>(args)...);
                debug_reporter::get_instance().reusable_alloc_size(sz, typeid(reusable), targs);
            }
#endif
        }

        ///allocator for normal function
        template<typename ... Args>
        void *operator new(std::size_t sz, Storage &m, Args &&...args ) {
            report_size(sz, std::forward<Args>(args)...);
            return m.alloc(sz);
        }
        ///deallocator for normal function when exception is thrown
        template<typename ... Args>
        void operator delete(void *ptr, Storage &m, Args &&... ) {
            m.dealloc(ptr);
        }

        ///allocator for member function
        template<typename THIS, typename ... Args>
        void *operator new(std::size_t sz,THIS &&, Storage &m, Args &&... args) {
            report_size(sz, std::forward<Args>(args)...);
            return m.alloc(sz);
        }
        ///deallocator for member function when exception is thrown
        template<typename THIS,typename ... Args>
        void operator delete( void *ptr, THIS &&,Storage &m, Args &&... ) {
            m.dealloc(ptr);
        }

        ///deallocator when coroutine finishes normally
        void operator delete(void *ptr, std::size_t) {
            reusable_memory<void>::generic_delete(ptr);
        }

        ///cast return object
        reusable<Coro, Storage> get_return_object() {
            return reusable<Coro, Storage>(std::move(Coro::promise_type::get_return_object()));
        }
    private:
        //reusable promise needs argument reusable_memory 
        void *operator new(std::size_t ) {throw std::bad_alloc();};
    };
};


///Contains coroutine and awaiter as one piece, while coroutine frame is allocated inside of awaiter
/**
 * @tparam T type of return value
 * @tparam reserved_space reserved space for  coroutine frame - should be large enough - you must guess :)
 * @tparam Args arguments of coroutine - required
 *
 * It is better to create startup function, which inicializes the awaiter with arguments and
 * coroutine itself
 *
 *
 * @note @b LIMITATION - the coroutine must be static, cannot be method of an object. You can
 * eventually pass this as argument
 */
template<typename T, std::size_t reserved_space, typename ... Args>
class elided_task_awaiter {
public:

    ///Storage - must be first argument of the coroutine
    using Storage = reusable_memory<std::array<void *, reserved_space> >;
    ///TaskType - return value of the coroutine (declaration)
    using TaskType  = reusable<task<T>, Storage>;
    ///Whole prototype of the coroutine
    using FunctionPtr = TaskType (*)(Storage &, Args ...);

    ///Construct the awaiter, start the coroutine
    /**
     * @param fn pointer to coroutine
     * @param args arguments of coroutine
     */
    elided_task_awaiter(FunctionPtr fn, Args && ... args) {
        _task = fn(_storage, std::forward<Args>(args)...);
    }
    ///object can't be copied
    elided_task_awaiter(const elided_task_awaiter &) = delete;
    ///object can't be assigned
    elided_task_awaiter &operator=(const elided_task_awaiter &) = delete;


    ///retrieve awaitable object
    /**
     * @return something you can await
     *
     * @note ensure, that this object is not destroyed during awaition!
     */
    co_awaiter<task_promise<T>,true> operator co_await() {
        return _task.operator co_await();
    }


protected:
    ///Contains storage
     Storage _storage;
     ///Contains task
    TaskType _task;
};

}



#endif /* SRC_COCLASSES_REUSABLE_H_ */
