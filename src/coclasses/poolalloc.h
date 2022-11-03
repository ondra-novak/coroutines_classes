/**
 * @file poolalloc.h
 * 
 *  Allocation pool
 * 
 * Helps to speedup memory allocation for coroutines
 * 
 * Every coroutine uses custom allocator which allocates memory through the POOLALLOC. You
 * can disable this feature by specifying macro COCLS_DISABLE_POOLALLOC. You can
 * also define your custom allocator through macro COCLS_USE_CUSTOM_ALLOCATOR where
 * you need to specify full qualified name of class which defines custom new/delete operators
 * 
 * POOLALLOC
 * 
 * When a coroutine exits, it drops memory block, which is collected in block cache. There
 * are several caches organized by size of the block. Block are allocated by a step, which
 * is (by default) about 80 bytes. So block with size 88 bytes will be rounded up to 160 bytes.
 * 
 * Block dropped to the cache can be reused for next allocation. Count of block dropped
 * to the cache is limited, additional blocks are deallocated through delete
 * 
 * Each thread has its own cache and there is also global cache. When local cache is empty.
 * it is refilled from global cache. When global cache is empty, it is refilled during
 * deallocation from local cache.
 * 
 * The most coroutines are probably finished in the same thread - so memory blocks are
 * retrieved from local cache and returned to the same cache. However, if the coroutine
 * ends in different thread, the block is returned to cache local for that thread. This
 * can also happen, that blocks are allocated in one thread and deallocated in other thread, 
 * this is the purpose of the global cache, which can be used to refill the local cache 
 * 
 * Allocation of the memory from the cache should be done in constant time without doing any
 * atomic stuff - only when the local cache is empty, then accessing the global cache need
 * atomic exchange. 
 * 
 * Deallocation of the memory always performs compare_exchange on global cache to check
 * whether the global cache is empty, which also refills this cache from the local cache
 * 
 * 
 */

#pragma once
#ifndef SRC_COCLASSES_SRC_COCLASSES_POOLALLOC_H_
#define SRC_COCLASSES_SRC_COCLASSES_POOLALLOC_H_
#include <atomic>
#include <cstddef>
#include <vector>
#include <array>
#include "common.h"



namespace cocls {

///Specify custom allocator for coroutines
#ifdef COCLS_USE_CUSTOM_ALLOCATOR
using coro_promise_base = COCLS_USE_CUSTOM_ALLOCATOR
#else
///Disable using poolalloc
#ifdef COCLS_DISABLE_POOLALLOC
class coro_promise_base {
public:
    void* operator new(std::size_t sz) {
        return ::operator new(sz);
    }
    void operator delete(void* ptr, std::size_t sz) {
        ::operator delete(ptr);
    }

};
#else

#ifndef COCLS_POOLALLOC_MAXCACHE
/** defines maximum bytes can be dropped into single cache
 *  additional dropped block are deallocated using standard allocator (operator delete)
 *  
 *  default value is 102400 (10KB).
 *  
 *  because it is for every level and every thread, you can calculate max cache allocation per thread 
 *  (for example 20*102400 = 2MB) per thread   
 *  
 *  however, it also depends, how many coroutines can your program call over time.
 */
 
#define COCLS_POOLALLOC_MAXCACHE 102400
#endif

#ifndef COCLS_POOLALLOC_ALLOCSTEP
/** defines allocation step
 *  
 *  so allocation requests are rounded to next alloc. step. Default is 8x size of pointer
 *  (80 bytes for 64b platform) 
 */
#define COCLS_POOLALLOC_ALLOCSTEP (sizeof(void *)*10)
#endif

#ifndef COCLS_POOLALLOC_LEVELS
/** defines max allocation levels which also defines maximum size can be handled by pool alloc
 * 
 *  COCLS_POOLALLOC_ALLOCSTEP * COCLS_POOLALLOC_LEVELS
 *  
 *  default is 20, which means 1600 bytes long block. Larger blocks are handled
 *  by standard new allocator 
 *  
 *  Note that there will be 20 atomic pointers and 40 normal pointers for every thread
 * 
 */
#define COCLS_POOLALLOC_LEVELS 20
#endif


namespace poolalloc {


template<std::size_t sz> 
struct block {
    union {
        block *next;
        char data[sz];
    };
    static void gc(block *x) {
        while (x) {
            auto y = x;
            x = x->next;
            ::operator delete(y);
        }
    }
};

template<std::size_t sz>
struct global_block_cache {
    std::atomic<block<sz> *> _chain;    
    
    block<sz> * swap_out_chain(block<sz> *chain) {
        return _chain.exchange(chain, std::memory_order_relaxed);
    }
    bool swap_chain_in(block<sz> * mychain) {
        block<sz> *exp = nullptr;
        return _chain.compare_exchange_strong(exp, mychain, std::memory_order_relaxed);
    }
    void gc() {
        auto x = swap_out_chain(nullptr);
        block<sz>::gc(x);
    }
    ~global_block_cache() {
        gc();
    }
};

struct abstract_thread_local_cache {
    virtual  ~abstract_thread_local_cache() = default;
    virtual void *alloc() = 0;
    virtual void dealloc(void *) = 0;
    
};

template<std::size_t sz>
struct thread_local_cache: abstract_thread_local_cache {
    global_block_cache<sz> *_cache;
    std::size_t _max_count;

    block<sz> * _prepared = nullptr;
    block<sz> * _dropped = nullptr;
    std::size_t _count = 0;
    
    thread_local_cache(global_block_cache<sz> *cache, std::size_t max_count)
        :_cache(cache), _max_count(max_count) {}
    
    virtual void *alloc() override {
        auto x = _prepared;
        if (!x) [[unlikely]] {
            x = _dropped;            
            _dropped = nullptr;
            _count = 0;
           
            if (!x) [[unlikely]] {
                x =  _cache->swap_out_chain(nullptr);            
            
                if (!x) [[unlikely]] {
                    return ::operator new(sz);
                }         
            }
        }
        _prepared = x->next;
        return x;
    }
    
    virtual void dealloc(void *ptr) override {
        block<sz> *b = reinterpret_cast<block<sz> *>(ptr);
        if (_count >= _max_count) [[unlikely]] {
            //if cache is full, check for refill global cache
            if (_cache->swap_chain_in(_dropped)) {
                //if refill succeed, put new item into empty local cache
                _dropped = b;
                _count = 1;
                return;
            } 
            //otherwise perform standard delete
            ::operator delete(ptr);
             return;            
        }
        //add block to cache
        ++_count;
        b->next = _dropped;
        _dropped = b;
        //check whether global cache is empty, if does, refill it
        if (_cache->swap_chain_in(_dropped)) [[unlikely]] {
            //refilled, so local cache is empty
            _count = 0;
            _dropped = nullptr;
        }        
    }
    
    void gc() {
        block<sz>::gc(_prepared);
        block<sz>::gc(_dropped);
        _count = 0;
        _prepared = nullptr;
        _dropped = nullptr;
    }
    
    ~thread_local_cache() {
        //before the local cache is destroyed, we can pass our blocks to global cache
        if (_dropped) {
            if (_cache->swap_chain_in(_dropped)) {
                _dropped = nullptr;
            }        
        } else if (_prepared) {
            if (_cache->swap_chain_in(_prepared)) {
                _prepared= nullptr;
            }                    
        }
        //run gc() now
        gc();
    }

    
};

template<std::size_t level, std::size_t step>
struct global_cache_chain {
    global_block_cache<step*level> _cache;
    global_cache_chain<level-1, step> _next;      
};
template<std::size_t step>
struct global_cache_chain<0,step> {

};


template<std::size_t level, std::size_t step>
struct thread_local_cache_chain {
    static constexpr std::size_t _size = level*step; 
    thread_local_cache<_size> _cache;
    thread_local_cache_chain<level-1, step> _next;
    
    thread_local_cache_chain(global_cache_chain<level, step> *l, std::size_t max_cache_size)
        :_cache(&l->_cache, max_cache_size/_size), _next(&(l->_next), max_cache_size) {}
    
    void *alloc(std::size_t sz) {
        if (sz == _size) {
            return _cache.alloc();
        }
        return _next.alloc(sz);
    }
    void dealloc(void *ptr, std::size_t sz) {
        if (sz == _size) {
            _cache.dealloc(ptr);
            return;
        }
        _next.dealloc(ptr, sz);
        return;
    }
    
    abstract_thread_local_cache **init_map(abstract_thread_local_cache **map) {
        map = _next.init_map(map);
        map++;
        *map = &_cache;
        return map;
    }
    
    void gc() {
        _cache.gc();
        _next.gc();
    }
};

template<std::size_t step>
struct thread_local_cache_chain<0,step> {
    thread_local_cache_chain(global_cache_chain<0, step> *l, std::size_t max_cache_size) {}
    void *alloc(std::size_t) {return nullptr;}
    void dealloc(void *, std::size_t) {}
    void gc() {}
    abstract_thread_local_cache **init_map(abstract_thread_local_cache **map) {return map-1;}
};


struct alloc_master {
    static constexpr std::size_t _max_cache_size = COCLS_POOLALLOC_MAXCACHE;
    static constexpr std::size_t _alloc_step = COCLS_POOLALLOC_ALLOCSTEP;
    static constexpr std::size_t _max_levels = COCLS_POOLALLOC_LEVELS;
    static constexpr std::size_t _max_alloc_size = _alloc_step * _max_levels;

    class ForceDelete: public abstract_thread_local_cache {
    public:
        virtual void *alloc() {throw std::bad_alloc();}
        virtual void dealloc(void *p) {::operator delete(p);}
    };
    
    static ForceDelete force_delete_instance;
    
    struct Local {
        std::array<abstract_thread_local_cache *, _max_levels> _map;
        thread_local_cache_chain<_max_levels, _alloc_step> _chain;
        
        Local()
           :_chain(&global_instance, _max_alloc_size) {
            _chain.init_map(_map.data());
        }
        ~Local() {
            for (auto &x: _map) {
                x = &force_delete_instance;
            }
        }
        static constexpr auto index(std::size_t sz) {return (sz-1)/_alloc_step;}
        void *alloc(std::size_t sz) {
            return _map[index(sz)]->alloc();
        }
        void dealloc(void *ptr, std::size_t sz) {            
            _map[index(sz)]->dealloc(ptr);
        }
            
    };
    
    using Global = global_cache_chain<_max_levels, _alloc_step>;
    static Global global_instance;
    static thread_local Local local_instance;
    
    
    
    static void *mem_alloc(std::size_t sz) {
        if (sz > _max_alloc_size) return ::operator new(sz);        
        return local_instance.alloc(sz);
    }
    static void mem_dealloc(void *ptr, std::size_t sz) {
        if (sz > _max_alloc_size) return ::operator delete(ptr);
        return local_instance.dealloc(ptr, sz);
    }
    
};

inline alloc_master::ForceDelete alloc_master::force_delete_instance;
inline alloc_master::Global alloc_master::global_instance;
inline thread_local alloc_master::Local alloc_master::local_instance;


}

///base class for every coroutine promise
/**
 * The main purpose is to define custom new / delete operators
 */
class coro_promise_base  {
public:
    void *operator new(std::size_t sz) {
        return poolalloc::alloc_master::mem_alloc(sz);
    }
    void operator delete(void *ptr, std::size_t sz) {
        poolalloc::alloc_master::mem_dealloc(ptr, sz);
    }    
};

#endif
#endif
}




#endif /* SRC_COCLASSES_SRC_COCLASSES_POOLALLOC_H_ */
