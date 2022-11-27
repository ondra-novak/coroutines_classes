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
 *  so allocation requests are rounded to next alloc. step. Default is 12x size of pointer
 *  (96 bytes for 64b platform) 
 */
#define COCLS_POOLALLOC_ALLOCSTEP (sizeof(void *)*12)
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

template<std::size_t sz>
struct thread_local_cache {
    global_block_cache<sz> *_cache;
    std::size_t _max_count;

    block<sz> * _prepared = nullptr;
    block<sz> * _dropped = nullptr;
    std::size_t _count = 0;
    
    thread_local_cache(global_block_cache<sz> *cache, std::size_t max_count)
        :_cache(cache), _max_count(max_count) {}
    
    void *alloc()  {
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
    
    void dealloc(void *ptr)  {
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

struct alloc_master {
    static constexpr std::size_t _max_cache_size = COCLS_POOLALLOC_MAXCACHE;
    static constexpr std::size_t _alloc_step = COCLS_POOLALLOC_ALLOCSTEP;
    static constexpr std::size_t _max_levels = 20;
    static constexpr std::size_t _max_alloc_size = _alloc_step * _max_levels;
    static consteval std::size_t max_cache_size(int lvl) {
        return _max_cache_size/(_alloc_step * lvl);
    }

    struct Global {
        global_block_cache<1*_alloc_step> _l1;
        global_block_cache<2*_alloc_step> _l2;
        global_block_cache<3*_alloc_step> _l3;
        global_block_cache<4*_alloc_step> _l4;
        global_block_cache<5*_alloc_step> _l5;
        global_block_cache<6*_alloc_step> _l6;
        global_block_cache<7*_alloc_step> _l7;
        global_block_cache<8*_alloc_step> _l8;
        global_block_cache<9*_alloc_step> _l9;
        global_block_cache<10*_alloc_step> _l10;
        global_block_cache<11*_alloc_step> _l11;
        global_block_cache<12*_alloc_step> _l12;
        global_block_cache<13*_alloc_step> _l13;
        global_block_cache<14*_alloc_step> _l14;
        global_block_cache<15*_alloc_step> _l15;
        global_block_cache<16*_alloc_step> _l16;
        global_block_cache<17*_alloc_step> _l17;
        global_block_cache<18*_alloc_step> _l18;
        global_block_cache<19*_alloc_step> _l19;
        global_block_cache<20*_alloc_step> _l20;        
        
    };
    
    struct Local {
        thread_local_cache<1*_alloc_step> _l1;
        thread_local_cache<2*_alloc_step> _l2;
        thread_local_cache<3*_alloc_step> _l3;
        thread_local_cache<4*_alloc_step> _l4;
        thread_local_cache<5*_alloc_step> _l5;
        thread_local_cache<6*_alloc_step> _l6;
        thread_local_cache<7*_alloc_step> _l7;
        thread_local_cache<8*_alloc_step> _l8;
        thread_local_cache<9*_alloc_step> _l9;
        thread_local_cache<10*_alloc_step> _l10;
        thread_local_cache<11*_alloc_step> _l11;
        thread_local_cache<12*_alloc_step> _l12;
        thread_local_cache<13*_alloc_step> _l13;
        thread_local_cache<14*_alloc_step> _l14;
        thread_local_cache<15*_alloc_step> _l15;
        thread_local_cache<16*_alloc_step> _l16;
        thread_local_cache<17*_alloc_step> _l17;
        thread_local_cache<18*_alloc_step> _l18;
        thread_local_cache<19*_alloc_step> _l19;
        thread_local_cache<20*_alloc_step> _l20;        

        Local(Global &g)
            :_l1(&g._l1, max_cache_size(1)) 
            ,_l2(&g._l2, max_cache_size(2))
            ,_l3(&g._l3, max_cache_size(3))
            ,_l4(&g._l4, max_cache_size(4))
            ,_l5(&g._l5, max_cache_size(5))
            ,_l6(&g._l6, max_cache_size(6))
            ,_l7(&g._l7, max_cache_size(7))
            ,_l8(&g._l8, max_cache_size(8))
            ,_l9(&g._l9, max_cache_size(9))
            ,_l10(&g._l10, max_cache_size(10))
            ,_l11(&g._l11, max_cache_size(11))
            ,_l12(&g._l12, max_cache_size(12))
            ,_l13(&g._l13, max_cache_size(13))
            ,_l14(&g._l14, max_cache_size(14))
            ,_l15(&g._l15, max_cache_size(15))
            ,_l16(&g._l16, max_cache_size(16))
            ,_l17(&g._l17, max_cache_size(17))
            ,_l18(&g._l18, max_cache_size(18))
            ,_l19(&g._l19, max_cache_size(19))
            ,_l20(&g._l20, max_cache_size(20))
        {}
        
        template<typename Fn>
        inline auto find_step(int i, Fn &&fn) {
            switch (i) {
                case 1: return fn(_l1);
                case 2: return fn(_l2);
                case 3: return fn(_l3);
                case 4: return fn(_l4);
                case 5: return fn(_l5);
                case 6: return fn(_l6);
                case 7: return fn(_l7);
                case 8: return fn(_l8);
                case 9: return fn(_l9);
                case 10: return fn(_l10);
                case 11: return fn(_l11);
                case 12: return fn(_l12);
                case 13: return fn(_l13);
                case 14: return fn(_l14);
                case 15: return fn(_l15);
                case 16: return fn(_l16);
                case 17: return fn(_l17);
                case 18: return fn(_l18);
                case 19: return fn(_l19);
                case 20: return fn(_l20);
                default: throw std::bad_alloc(); //replace by std::unreachable in C++23
            }
        }
                
        inline void *alloc(std::size_t sz) {
            int idx = static_cast<int>((sz +_alloc_step-1)/_alloc_step);
            return find_step(idx, [](auto &x){
                   return x.alloc();
            });
        }
        inline void dealloc(void *ptr, std::size_t sz) {
            int idx = static_cast<int>((sz +_alloc_step-1)/_alloc_step);
            find_step(idx, [ptr](auto &x){
                x.dealloc(ptr);
            });
        }
            
    };
    
    static Global global_instance;
    static thread_local Local local_instance;

    static void *mem_alloc(std::size_t sz) {
        if (sz > _max_alloc_size) [[unlikely]] return ::operator new(sz);        
        return local_instance.alloc(sz);
    }
    static void mem_dealloc(void *ptr, std::size_t sz) {
        if (sz > _max_alloc_size) [[unlikely]] return ::operator delete(ptr);
        return local_instance.dealloc(ptr, sz);
    }
    
};

inline alloc_master::Global alloc_master::global_instance;
inline thread_local alloc_master::Local alloc_master::local_instance(global_instance);


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
