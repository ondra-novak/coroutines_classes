/*
 * reusable.h
 *
 *  Created on: 8. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_REUSABLE_H_
#define SRC_COCLASSES_REUSABLE_H_
#include <vector>
#include <new>

namespace cocls {


///Reusable buffer for reusable coroutine
/**
 * @see reusable
 */
class reusable_memory {
public:
    
    struct block {
        reusable_memory *owner;
        char data[1];
    };

    ///Allocate block
    /**
     * This object can hold only one block. If block is already allocated, standard new is used
     * 
     * @note not MT Safe
     * @param sz requested size
     */
    void *alloc(std::size_t sz) {
        std::size_t nsz = sz+sizeof(block::owner);
        block *b;
        if (_busy) {
            b = reinterpret_cast<block *>(::operator new(nsz));
            b->owner = nullptr;
        } else {
            _buffer.resize(nsz);
            b = reinterpret_cast<block *>(_buffer.data());
            b->owner = this;
            _busy = true;
        }
        return std::launder(b->data);
    }
    ///Deallocate block allocated by alloc
    /**
     * 
     * @param ptr pointer to block returned by alloc()
     */
    static void dealloc(void *ptr) {
        block *b = reinterpret_cast<block *>(reinterpret_cast<char *>(ptr) - sizeof(block::owner));
        if (b->owner == nullptr) ::operator delete(b);
        else {
            b->owner->_busy = false;
        }
    }
    
    
protected:
    bool _busy = false;
    std::vector<char> _buffer;
    
    
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
template<typename Coro>
class reusable: public Coro {
public:
    
    reusable(Coro &&x):Coro(std::move(x)) {}
    reusable() {}
    
    class promise_type: public Coro::promise_type {
    public:
        
        template<typename ... Args>
        void *operator new(std::size_t sz, reusable_memory &m, Args &&... ) {
            return m.alloc(sz);
        }
        template<typename ... Args>
        void operator delete(void *ptr, reusable_memory &m, Args &&... ) {
            return m.dealloc(ptr);
        }
        
        void operator delete(void *ptr, std::size_t) {
            return reusable_memory::dealloc(ptr);
        }

        reusable<Coro> get_return_object() {
            return reusable<Coro>(std::move(Coro::promise_type::get_return_object()));
        }
    private:
        //reusable promise needs argument reusable_memory 
        void *operator new(std::size_t ) {throw std::bad_alloc();};
    };
};
}



#endif /* SRC_COCLASSES_REUSABLE_H_ */
