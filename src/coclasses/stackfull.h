/*
 * stackfull.h
 *
 *  Created on: 16. 2. 2023
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_STACKFULL_H_
#define SRC_COCLASSES_STACKFULL_H_
#include <cstddef>
#include <cassert>
#include <stack>
#include <utility>


namespace cocls {

///Stack instance to enable stackfull coroutines
/**Stackfull coroutines are coroutines, which are declared std::with_allocator<coro_stack,...>
 * Stack is carried as first argument of the coroutine (as reference) the coroutine
 * passes the stack to the next level of recursion.
 *
 * Recommended coroutine type is subtask<T> as this coroutine avoids
 * race access to the stack. You can also use stackfull<T> coroutine which is
 * typedef shortcut.
 *
 * The stack is constructed with a number, which specifies expected levels
 * of recursions. This number is just a hint, it preallocates memory for
 * first recursions. if the preallocated space is exhausted, new block
 * is allocated. If the coroutine repeatedly enters and exits recursion,
 * extra blocks can be merged to single larger block to reduce allocations even more.
 *
 * By constructing large enoght stack you can avoid nearly all allocations
 * for recursive calls of the coroutine.
 *
 * @note coro_stack is not MT safe. Frames must be released in reverse order
 * of creation.
 *
 */
class coro_stack {
public:

    explicit coro_stack(std::size_t levels):_multiplier(levels) {}

    ~coro_stack() {
        assert("releasing nonempty stack" && _top == 0);
    }


    void *alloc_block(std::size_t sz) {
        block_t *ab;
        if (_blocks.empty()) {
            ab = prepare_block(sz);
        } else {
            block_t &b = _blocks[_top-1];
            if (b._top+ sz > b._size) {
                ab = prepare_block(sz);
            } else {
                ab = &b;
            }
        }
        void *r = ab->offset(ab->_top);
        assert(ab->_top + sz <= ab->_size);
        ab->_top += sz;

        return r;
    }

    void dealloc_block(void *ptr, std::size_t sz) {
        assert("FATAL: Stack is empty" && !_blocks.empty());
        block_t &b = _blocks[_top-1];
        assert("FATAL: stack isn't released in correct order" && b.distance(ptr)+sz == b._top);
        b._top -= sz;
        if (b._top == 0) {
            _top--;
        }
    }

    void *alloc(std::size_t sz) {
        void *ptr = alloc_block(sz + sizeof(coro_stack **));
        coro_stack **stor = reinterpret_cast<coro_stack **>(reinterpret_cast<uint8_t *>(ptr)+sz);
        *stor = this;
        return ptr;
    }

    static void dealloc(void *ptr, std::size_t sz) {
        coro_stack **stor = reinterpret_cast<coro_stack **>(reinterpret_cast<uint8_t *>(ptr)+sz);
        coro_stack *me = *stor;
        me->dealloc_block(ptr, sz+sizeof(coro_stack **));
    }

protected:

    struct block_t {
        void *_ptr = nullptr;
        std::size_t _size = 0;
        std::size_t _top = 0;
        block_t() = default;
        block_t(std::size_t sz)
            :_ptr(::operator new(sz))
            ,_size(sz)
            ,_top(0) {}
        ~block_t() {
            ::operator delete(_ptr);
        }
        block_t(const block_t &) = delete;
        block_t &operator= (const block_t &) = delete;
        block_t(block_t &&o)
            :_ptr(std::exchange(o._ptr,nullptr))
            ,_size(o._size)
            ,_top(o._top) {}
        block_t &operator= (block_t &&o) {
            if (this != &o) {
                ::operator delete(_ptr);
                _ptr = std::exchange(o._ptr, nullptr);
                _size = o._size;
                _top = o._top;
            }
            return *this;
        }


        std::size_t distance(const void *ptr) const {
            return reinterpret_cast<const uint8_t *>(ptr) - reinterpret_cast<const uint8_t *>(_ptr);
        }
        const void *offset(std::size_t sz) const {
            return reinterpret_cast<const uint8_t *>(_ptr) + sz;
        }
        void *offset(std::size_t sz) {
            return reinterpret_cast<uint8_t *>(_ptr) + sz;
        }
    };

    std::vector<block_t> _blocks;
    std::size_t _top = 0;
    std::size_t _multiplier;

    block_t *prepare_block(std::size_t sz) {
        std::size_t msize = 0;
        std::size_t extra = _blocks.size() - _top;
        if (extra > 1) {
            for (std::size_t i = _top, cnt = _blocks.size(); i < cnt; ++i) {
                msize += _blocks[i]._size;
            }
            msize = std::max(msize, sz);
        } else if (extra == 1) {
            if (_blocks[_top]._size >= sz) {
                ++_top;
                return &_blocks[_top-1];
            }
            msize = sz * _multiplier;
        } else {
            msize = sz * _multiplier;
        }
        _blocks.resize(_top);
        _blocks.push_back(block_t(msize));
        ++_top;
        return &_blocks[_top-1];
    }

};

template<typename T>
class subtask;

///Specifies stackfull coroutine.
/**
 * The stack is passed as first argument.
 */
template<typename T>
using stackfull = cocls::with_allocator<coro_stack, cocls::subtask<T> >;

}


#endif /* SRC_COCLASSES_STACKFULL_H_ */
