#pragma once
#ifndef SRC_COCLASSES_NO_ALLOC_H_
#define SRC_COCLASSES_NO_ALLOC_H_
#include <array>
#include <atomic>
#include <cstddef>
#include <vector>


#ifndef COCLS_STATIC_STORAGE_MULTIPLIER
///Controls multiplier of all static storage
/**
 * It can happen, that preallocated static storage is too small for given compiler.
 * This multiplier can increase this space for all allocation. 
 * Define this constant as macro on command line
 * Value is in percent
 * 
 * Default value is 100 => 1x. Set 150 => 1.5x. 200 => 2x, etc
 */
#define COCLS_STATIC_STORAGE_MULTIPLIER 100
#endif


namespace cocls {


///Coroutine which is allocated in specified storage
/**
 * @tparam Coro Coroutine type (task<T>)
 * @tparam Storage Storage type
 * 
 * To use this coroutine, the first argument must be Storage & instance
 * 
 * @code
 * no_alloc<task<>, storage_t<> > test_coroutine(storage_t<> &s, ...)
 * @endcode
 */
template<typename Coro, typename Storage>
class no_alloc: public Coro {
public:
    no_alloc(Coro &&coro):Coro(std::move(coro)) {}
    
    
    class promise_type: public Coro::promise_type {
    public:
        
        ///allocator for normal function
        template<typename ... Args>
        constexpr void *operator new(std::size_t sz, Storage &m, Args &&...args ) {
            return m.alloc(sz);
        }
        ///deallocator for normal function when exception is thrown
        template<typename ... Args>
        constexpr void operator delete(void *ptr, Storage &m, Args &&... ) {
            m.dealloc(ptr);
        }

        ///allocator for member function
        template<typename THIS, typename ... Args>
        constexpr void *operator new(std::size_t sz,THIS &&, Storage &m, Args &&... args) {
            return m.alloc(sz);
        }
        ///deallocator for member function when exception is thrown
        template<typename THIS, typename ... Args>
        constexpr void operator delete( void *ptr, THIS &&,Storage &m, Args &&... ) {
            m.dealloc(ptr);
        }

        ///deallocator when coroutine finishes normally
        constexpr void operator delete(void *ptr, std::size_t) {
            Storage::dealloc(ptr);
        }

        ///cast return object
        no_alloc<Coro, Storage> get_return_object() {
            return no_alloc<Coro, Storage>(std::move(Coro::promise_type::get_return_object()));
        }
    private:
        //reusable promise needs argument reusable_memory 
        void *operator new(std::size_t ) {throw std::bad_alloc();};
    };
    
};



template<typename T = std::vector<char> >
class storage_t {
public:
    
    ///Allocate block
    /**
     * This object can hold only one block. If block is already allocated, standard new is used
     * 
     * @note not MT Safe
     * @param sz requested size
     */
    constexpr void *alloc(std::size_t sz) {
        auto nsz = sz + sizeof(storage_t *);
        void *ptr;
        storage_t *sptr;
        bool r = _busy.exchange(true);
        if (r) [[unlikely]] {
            ptr = ::operator new(nsz);
            sptr = nullptr;
        } else {
            _buffer.resize(nsz);
            ptr = _buffer.data();
            sptr = this;
        }        
        storage_t **me = reinterpret_cast<storage_t **>(ptr);
        *me = sptr;
        return me+1;
    }
    ///Deallocate block allocated by alloc
    /**
     * 
     * @param ptr pointer to block returned by alloc()
     */
    constexpr static void dealloc(void *ptr) {
        storage_t **sptr = reinterpret_cast<storage_t **>(ptr)-1;
        storage_t *me = *sptr;
        if (!me) [[unlikely]]  {            
            ::operator delete(sptr);
            return;
        }          
        me->_busy = false;
    }
    
    
protected:
    std::atomic<bool> _busy = false;
    T _buffer;
};


class static_storage_too_small: public std::exception {
public:
    static_storage_too_small(std::size_t need, std::size_t avail, std::size_t orig_avail)
        :need(need), avail(avail), orig_avail(orig_avail) {}
    const char *what() const noexcept override {
        if (msg.empty()) {
            std::size_t sugmult = (need*100+orig_avail-1)/orig_avail;
            /* C++20 format is not supported yet! */
            /*msg = std::format("Static storage is too small. required {}, available {}, suggested multiplier {}", need, avail, sugmult);*/
            msg.append("Static storage is too small. required ");
            msg.append(std::to_string(need));
            msg.append(", available ");
            msg.append(std::to_string(avail));
            msg.append(", suggested multiplier ");
            msg.append(std::to_string(sugmult));
        }
        return msg.c_str();
    }
    
protected:
    std::size_t need;
    std::size_t avail;
    std::size_t orig_avail;
    mutable std::string msg;
    
};

template<std::size_t sz>
class static_storage_buffer_t {
public:

    static constexpr std::size_t _size = sz * (COCLS_STATIC_STORAGE_MULTIPLIER)/100; 
    
    
    static constexpr void resize(unsigned int s) {
        if (s > _size) {
            throw static_storage_too_small(s, _size, sz);
        }
    }
    
    void *data() {
        return _buffer.data();
    }
    
protected:
    std::array<char, _size> _buffer;
};

template<std::size_t sz>
class static_storage_t {
public:
    static constexpr std::size_t _size = sz * (COCLS_STATIC_STORAGE_MULTIPLIER)/100; 

    ///Allocate block
     /**
      * This object can hold only one block. If block is already allocated, standard new is used
      * 
      * @note not MT Safe
      * @param sz requested size
      */
     void *alloc(std::size_t s) {         
         if (s > _size) {
             throw static_storage_too_small(s, _size, sz);
         } else {
             return _buffer;
         }
     }
     ///Deallocate block allocated by alloc
     /**
      * 
      * @param ptr pointer to block returned by alloc()
      */
     constexpr static void dealloc(void *) {
     }
    
     char _buffer[_size];    
};



}
#endif /* SRC_COCLASSES_NO_ALLOC_H_ */

