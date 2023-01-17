/**
 * @file co_new.h 
 * handles custom coroutine allocation controlled by the caller
 */
#ifndef SRC_COCLASSES_CO_ALLOC_H_
#define SRC_COCLASSES_CO_ALLOC_H_
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
    
    class scope;
    
    template<typename Fn>
    auto call(Fn &&fn) -> decltype(fn()); 
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
        coro_storage *s = coro_next_storage;
        void *ptr = nullptr;
        if (s) {
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

class coro_storage::scope {
public:
    scope(coro_storage *s) {coro_allocator::coro_next_storage = s;}
    ~scope() {coro_allocator::coro_next_storage = nullptr;}    
};

template<typename Fn>
auto coro_storage::call(Fn &&fn) -> decltype(fn()) {
    scope _(this);
    return fn();
}


}




#endif /* SRC_COCLASSES_CO_ALLOC_H_ */
