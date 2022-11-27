/**
 * @file co_new.h 
 * handles custom coroutine allocation controlled by the caller
 */
#ifndef SRC_COCLASSES_CO_NEW_H_
#define SRC_COCLASSES_CO_NEW_H_
#include "poolalloc.h"
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
};

///Base class for all coroutines that supports the function co_new()
/**
 * defines general allocators 
 */
class coro_allocator: public coro_promise_base {
public:

    static thread_local coro_storage *coro_next_storage;

    
    void *operator new(std::size_t sz) {
        std::size_t needsz = sz + sizeof(coro_storage *);
        coro_storage *s = nullptr;
        void *ptr = nullptr;
        if (coro_next_storage) {
            s = coro_next_storage;
            coro_next_storage = nullptr;
            ptr = s->alloc(needsz);
        } else {
            ptr = coro_promise_base::operator new(needsz);
        }
        coro_storage **store_ptr = reinterpret_cast<coro_storage **>(reinterpret_cast<char *>(ptr)+sz);
        *store_ptr = s;
        return ptr;
    }
    
    void operator delete(void *ptr, std::size_t sz) {
        std::size_t needsz = sz + sizeof(coro_storage *);
        coro_storage **store_ptr = reinterpret_cast<coro_storage **>(reinterpret_cast<char *>(ptr)+sz);
        coro_storage *s = *store_ptr;
        if (s) {
            s->dealloc(ptr, needsz);
        } else {
            coro_promise_base::operator delete(ptr, needsz);
        }
    }
};

inline thread_local coro_storage *coro_allocator::coro_next_storage = nullptr;

namespace _details {
    template<typename Fn>
    class alloc_coro {
    public:
        alloc_coro(coro_storage &stor, Fn &fn): _fn(fn) {
            coro_allocator::coro_next_storage = &stor;
        }
        ~alloc_coro() {
            coro_allocator::coro_next_storage = nullptr;
        }
        template<typename ... Args>
        decltype(auto) operator()(Args && ... args) {
            return _fn(std::forward<Args>(args)...);
        }
        
    protected:
        Fn &_fn;
    };

    template<typename Obj, typename Fn>
    class alloc_coro_mfn {
    public:
        alloc_coro_mfn(coro_storage &stor, Obj &obj, Fn &fn): _obj(obj),_fn(fn) {
            coro_allocator::coro_next_storage = &stor;
        }
        ~alloc_coro_mfn() {
            coro_allocator::coro_next_storage = nullptr;
        }
        template<typename ... Args>
        decltype(auto) operator()(Args && ... args) {
            return ((*_obj).*_fn)(std::forward<Args>(args)...);
        }
        
    protected:
        Obj &_obj;
        Fn &_fn;
    };
}

///Allocate coroutine frame using specified allocator
/**
 * @param stor allocator instance. Ensure that instance stays valid while coroutine is running
 * @param fn function to be allocated and executed  
 *          there can also non-coroutine lambda, but need call the coroutine inside of the lambda
 *          You are allowed to call exactly one coroutine, the allocator is not applied for other
 *          coroutines called inside of the lambda
 * @return function which can be called with arguments which are passed to the coroutine. The
 * function returns result of the coroutine
 * 
 * @code
 *    auto task = co_new(allocator, coroutine_fn)(arg1, arg2, arg3...);
 * @endcode
 * 
 * @note Coroutine types supported: task, generator, stoppable_generator, lazy
 * 
 */
template<typename Fn>
auto co_new(coro_storage &stor, Fn &&fn) {
    return _details::alloc_coro<std::remove_reference_t<Fn> >(stor, fn); 
}

///Allocate coroutine frame using specified allocator. This allows to call member coroutine 
/**
 * @param stor allocator instance. Ensure that instance stays valid while coroutine is running
 * @param obj pointer to object. Variable can be either a raw pointer or a smart pointer. It needs
 *  to support dereference operator *
 * @param fn pointer to member coroutine to be called example: &Foo::bar
 * @return function which can be called with arguments which are passed to the coroutine. The
 * function returns result of the coroutine
 * 
 * @code
 *    auto task = co_new(allocator, this, &Foo::coroutine)(arg1, arg2, arg3...);
 * @endcode
 * 
 * @note Coroutine types supported: task, generator, stoppable_generator, lazy
 */
template<typename Obj, typename Fn>
auto co_new(coro_storage &stor, Obj &&obj, Fn &&fn) {
    return _details::alloc_coro_mfn<std::remove_reference_t<Obj>, std::remove_reference_t<Fn> >(stor, obj, fn); 
}

}




#endif /* SRC_COCLASSES_CO_NEW_H_ */
