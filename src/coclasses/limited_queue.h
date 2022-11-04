/**
 * @file queue.h
 */
#pragma once
#ifndef SRC_COCLASSES_LIMITED_QUEUE_H_
#define SRC_COCLASSES_LIMITED_QUEUE_H_

#include "queue.h"

namespace cocls {

    namespace _details {


        template<class T>
        class limited_queue_impl {
        public:

            union item {
                T _subject;
                char _buffer[sizeof(T)];
                item() {}
                ~item() {}
            };


            void reserve(std::size_t sz) {
                clear();
                _items.resize(sz + 1); //alloc +1 item, to distinguish between empty and full queue
                _beg = _end = 0;
            }

            void clear() {
                while (!empty()) pop();
            }

            bool empty() const {
                return _beg == _end;
            }

            template<typename ... Args>
            void emplace(Args && ... args) {
                std::size_t newend = (_end + 1) % _items.size();
                if (newend == _end) throw std::runtime_error("Limited queue is full");
                new (_items[_end]._buffer) T(std::forward<Args>(args)...);
                _end = newend;
            }

            void push(T&& v) {
                emplace(std::move(v));
            }
            void push(const T& v) {
                emplace(v);
            }

            std::size_t size() const {
                auto sz = _items.size();
                return (_end + sz - _beg) % sz;
            }

            bool full() const {
                auto sz = _items.size();
                return (_beg + 1) % sz == _end;
            }

            std::size_t capacity() const {
                return _items.size() - 1;
            }

            T& front() {
                //reading empty queue is UB
                return _items[_beg]._subject;
            }

            const T& front() const {
                //reading empty queue is UB
                return _items[_beg]._subject;
            }

            void pop() {
                //pop from empty queue is UB;
                _items[_beg]._subject.~T();
                _beg = (_beg + 1) % _items.size();
            }
            ~limited_queue_impl() {
                clear();
            }
        protected:
            std::vector<item> _items;
            std::size_t _beg = 0;
            std::size_t _end = 0;

        };


    }

    ///Limited queue
    /**
     * works as queue<>, queue has limited length. It is slightly faster,
     * queue allocation is done only once - at the beginning
     *
     * @tparam T type of item
     * @tparam CoroQueue type of queue of awaiters
     * @tparam Lock type of lock
     */
    template<typename T, typename CoroQueue = std::queue<abstract_awaiter<>*>, typename Lock = std::mutex>
    class limited_queue : public queue<T, _details::limited_queue_impl<T>, CoroQueue, Lock> {
    public:
        ///construct queue, specify size
        /**
         * @param sz size of queue (must be >0)
         */
        limited_queue(std::size_t sz) {
            this->_queue.reserve(sz);
        }
    };
    /// limited queue, where function push is also awaitable, because the pusher - producer - can be blocked on full queue
    /**
    * @tparam T type of ite,
    * @tparam PQueue queue implementation for producers
    * @tparam CQueue queue implemenration for consumers
    * @tparam Lock lock implementation
    */
    template<typename T, typename PQueue = std::queue<abstract_awaiter<>* >, typename CQueue = std::queue<abstract_awaiter<>* >, typename Lock = std::mutex>
    class limited_queue_awaitable_push : public limited_queue<T, CQueue, Lock> {
    public:
        using super_t = limited_queue<T, CQueue, Lock>;

        class push_awaiter : public co_awaiter<limited_queue_awaitable_push> {
        public:
            push_awaiter(co_awaiter<limited_queue_awaitable_push>& owner, T &&item)
                :co_awaiter<limited_queue_awaitable_push>(owner), _item(std::move(item)) {}
            push_awaiter(co_awaiter<limited_queue_awaitable_push>& owner, const T& item)
                :co_awaiter<limited_queue_awaitable_push>(owner), _item(item) {}
            push_awaiter(const push_awaiter& other) = default;
            push_awaiter& operator=(const push_awaiter& other) = delete;

            bool await_ready() {
                if (try_push(*_item)) {
                    _item.reset();
                    return true;
                }
                else {
                    return false;
                }
            }

            void await_resume() {
                if (_item.has_value()) {
                    final_push(*_item);
                }
            }
        protected:
            std::optional<T> _item;
        };

        class pop_awaiter : public co_awaiter<super_t> {
        public:
            using co_awaiter<super_t>::co_awaiter;

            decltype(auto) await_resume() {
                decltype(auto) x = co_awaiter<super_t>::await_resume();
                static_cast<limited_queue_awaitable_push&>(this->_owner).check_after_pop();
                return x;
            }

        };

        ///push items to the queue
        /**
        * @param item rvalue reference to an item
        * @return awaiter, so you need to co_await on result
        */
        push_awaiter push(T &&item) {
            push_awaiter(*this, &item);
        }

        ///push items to the queue
        /**
        * @param item lvalue reference to an item
        * @return awaiter, so you need to co_await on result
        */
        push_awaiter push(const T& item) {
            push_awaiter(*this, item);
        }

        /// pop item from the queue
        /**
        * @return awaiter, so you need to co_await on result
        */
        pop_awaiter pop() {
            return pop_awaiter(*this);
        }

        /// checks state of the queue after pop
        /** Ensures that every waiting push-awaiter is resumed in case, that queue
        * has a space to insert new items. This function is called automatically 
        * after pop() on this class. However, if the pop is done through the
        * its parent class (queue), this check is not done, so you must call it
        * manualy
        **/
        void check_after_pop() {
            abstract_awaiter<>* awt;
            while (this->_queue.size() + _reserved_space < this->_queue.capacity()) {
                {
                    {
                        std::lock_guard _(this->_mx);
                        if (_pqueue.empty()) return;
                        awt = _pqueue.front();
                        _pqueue.pop();
                        _reserved_space++;
                    }
                    awt->resume();
                }
            }
        }


    protected:
        PQueue _pqueue;
        friend class co_awaiter<limited_queue_awaitable_push>;
        std::size_t _reserved_space;

        bool try_push(T &item) {
            std::lock_guard _(this->_mx);
            if (this->_queue.size() + _reserved_space < this->_queue.capacity()) {
                this->_queue.push(std::move(item));
            }
            return false;
        }

        bool subscribe_awaiter(abstract_awaiter<>* awt) {
            std::lock_guard _(this->_mx);
            if (this->_queue.size() + _reserved_space < this->_queue.capacity()) {
                _reserved_space++;
                return false;
            }
            _pqueue.push(awt);
            return true;
        }

        void final_push(T &item) {
            std::lock_guard _(this->_mx);
            this->_queue.push(std::move(item));
            _reserved_space--;
        }

    };
}

#endif