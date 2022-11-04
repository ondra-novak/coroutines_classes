/**
 * @file publisher.h - publisher-subscriber pattern
 */

#pragma once
#ifndef SRC_COCLASSES_PUBLISHER_H_
#define SRC_COCLASSES_PUBLISHER_H_
#include "future.h"

#include <deque>
#include <mutex>
#include <optional>
#include <queue>
#include <set>

namespace cocls {


///subscription type
enum class subscribtion_type {
    ///read all values, no skipping, if the subscriber is left behind, it is dropped
    /** default mode, subscriber receives all published values */
    all_values,
    ///read all values, but if the subscriber is behind, the values are skipped    
    skip_if_behind,
    ///always skip to recent value, don't read historical values
    /**
     * useful, when subscriber doesn't need complete history 
     */
    skip_to_recent
    
};


template<typename T>
class subscriber;


///Publisher
/**
 * Object allows to publish values and register subscribers
 * 
 * @tparam T type of published value. The subscriber then 
 *          returns std::optional<T> which allows to 
 *          report EOF state 
 * 
 * Publisher contains a queue, which gives subscribers chance to catch values
 * if they are slower than publisher. You can also configure minimal queue size 
 * and maximal queue size.  
 */
template<typename T>
class publisher {
public:
    
    enum class read_mode {
    };
    
    class queue {
    public:
        
        queue() = default;
        queue(std::size_t max_queue_len, std::size_t min_queue_len = 1)
            :_max_queue_len(max_queue_len), _min_queue_len(min_queue_len)
        {
            assert(_min_queue_len > 0);
            assert(_max_queue_len >= _min_queue_len);
        }
        ///announce position
        void subscribe(const subscriber<T> *sub, std::size_t pos) {
            std::lock_guard _(_mx);
            subscribe_lk(sub,pos);
        }
        ///announce position - set to most recent
        std::size_t subscribe(const subscriber<T> *sub) {
            std::lock_guard _(_mx);
            return subscribe_lk(sub);
        }
        bool advance(const subscriber<T> *sub, std::size_t &pos, subscribtion_type type) {
            std::lock_guard _(_mx);
            return advance_lk(sub,pos,type);
        }
        bool advance_suspend(const subscriber<T> *sub, std::size_t &pos, abstract_awaiter<true> *awt) {
            std::lock_guard _(_mx);
            return advance_suspend_lk(sub,pos,awt);
        }
        
        void leave(const subscriber<T> *sub, std::size_t pos) {
            std::lock_guard _(_mx);
            leave_lk(sub, pos);
        }
        
        std::optional<T> get_value(const subscriber<T> *sub, std::size_t pos, subscribtion_type type) {
            std::lock_guard _(_mx);
            return get_value_lk(sub, pos,type);
        }
        void push(T &&val) {
            std::unique_lock<std::mutex> lk(_mx);
            _q.push_front(std::move(val));
            push_lk(lk);
        }
        void push(const T &val) {
            std::unique_lock<std::mutex> lk(_mx);
            _q.push_front(val);
            push_lk(lk);
        }
        void close() {
            std::unique_lock<std::mutex> lk(_mx);
            if (_closed) [[unlikely]] return;
            _pos--;
            _closed = true;
            push_lk(lk);
        }
        
    protected:
        using lock_pos = std::pair<std::size_t, const subscriber<T> *>;        

        template<typename X>
        struct allocator {
        public:
            allocator() = default;
            allocator(const allocator &other) {};
            template <class _Other>
            constexpr allocator(const allocator<_Other>&) noexcept {}
            allocator &operator=(const allocator &other) = delete;
            struct block {                 // @suppress("Miss copy constructor or assignment operator")
                block *_next;
                char _buffer[sizeof(X)];
            };
            X *allocate(auto n) {
                assert(n == 1);
                void *ret;
                if (blks) {
                    ret = blks->_buffer;
                    blks = blks->_next;
                } else {
                    block *b = new block;
                    ret = b->_buffer;
                }
                return reinterpret_cast<X *>(ret);
            }
            
            void deallocate(X *ptr, auto n) {
                assert(n == 1);
                block *b = reinterpret_cast<block *>(reinterpret_cast<char *>(ptr)-offsetof(block, _buffer));
                b->_next = blks;
                blks = b;
            }

            ~allocator() {
                while (blks) {
                    auto x = blks;
                    blks = blks->_next;
                    delete x;
                }
            }

            using size_type = size_t;
            using difference_type = ptrdiff_t;
            using propagate_on_container_move_assignment = std::true_type;
            using is_always_equal  = std::true_type;
            using _From_primary = allocator;
            
            using value_type = X;
            block *blks = nullptr;
        };
#if 0
        using pos_set = std::set<lock_pos, std::less<lock_pos>>; 
#else
        using pos_set = std::set<lock_pos, std::less<lock_pos>, allocator<lock_pos> >; 
#endif
        
        const std::size_t _max_queue_len = std::numeric_limits<std::size_t>::max();
        const std::size_t _min_queue_len = 1; 

        std::mutex _mx;
        std::deque<T> _q;
        pos_set _positions;
        std::size_t _pos = 0;
        abstract_awaiter<true> * _awaiters = nullptr;
        bool _closed = false;
        
        void subscribe_lk(const subscriber<T> *sub, std::size_t pos) {
            _positions.insert(lock_pos(pos, sub));
        }
        std::size_t subscribe_lk(const subscriber<T> *sub) {            
            _positions.insert(lock_pos(_pos-1, sub));
            return _pos-1;
        }
        void leave_lk(const subscriber<T> *sub, std::size_t pos) {
            _positions.erase(lock_pos(pos, sub));
        }
        bool advance_lk(const subscriber<T> *sub, std::size_t &pos, subscribtion_type t) {
            if (pos+1 == _pos && !_closed) return false;
            switch (t) {
                default:
                case subscribtion_type::all_values:
                    _positions.erase(lock_pos(pos, sub));
                    pos++;
                    _positions.insert(lock_pos(pos, sub));
                    break;
                case subscribtion_type::skip_if_behind:
                    _positions.erase(lock_pos(pos, sub));
                    pos = std::max(pos+1, _pos - _q.size());                    
                    _positions.insert(lock_pos(pos, sub));
                    break;
                case subscribtion_type::skip_to_recent:
                    _positions.erase(lock_pos(pos, sub));
                    pos = std::max(pos+1, _pos - 1);
                    _positions.insert(lock_pos(pos, sub));
                    break;
            }
            return true;
        }
        bool advance_suspend_lk(const subscriber<T> *sub, std::size_t &pos, abstract_awaiter<true> *awt) {
            _positions.erase(lock_pos(pos, sub));
            pos++;
            _positions.insert(lock_pos(pos, sub));
            if (pos == _pos) {
                awt->_next = _awaiters;
                _awaiters = awt;
            }
            return pos == _pos;
        }
        std::optional<T> get_value_lk(const subscriber<T> *sub, std::size_t pos, subscribtion_type type) {
            if (pos == _pos) return {};
            switch (type) {
                default:
                case subscribtion_type::all_values: {
                    std::size_t relpos = _pos - pos - 1;
                    if (relpos >= _q.size()) return {};
                    return _q[relpos];
                }
                case subscribtion_type::skip_if_behind: {
                    std::size_t relpos = _pos - pos - 1;
                    if (relpos >= _q.size()) relpos = _q.size()-1;
                    return _q[relpos];
                }
                case subscribtion_type::skip_to_recent: {
                    return _q[0];                    
                }
            }
            
        }
        void push_lk(std::unique_lock<std::mutex> &lk) {
            _pos++;
            std::size_t need_len = _min_queue_len;
            auto iter = _positions.begin();
            if (iter != _positions.end()) {
                need_len = std::max(_pos - iter->first,_min_queue_len);                
            }            
            _q.resize(std::min({need_len, _max_queue_len, _q.size()}));
            abstract_awaiter<true> *x = _awaiters;
            _awaiters = nullptr;
            lk.unlock();
            x->resume_chain_lk(x, nullptr);            
        }
        
        
    };
    
    ///Construct publisher with default settings
    /**
     * The default settings is
     *  - minimal queue size is set to 1.
     *  - maximal queue size is set to infinity
     */
    publisher():_q(std::make_shared<queue>()) {}

    ///Constructs publisher with settings
    /**
     * @param max_queue_len specifies maximum queue length. If there is subscriber too slow
     * to processes published values, it can be left behind and drop out. For that subscriber,
     * EOF is reported in this case
     * 
     * @param min_queue_len specifies minimal queue length kept even if all subscribers already
     * processed these values. This allows to subscriber to "reconnect" the queue at 
     * some position and if this position is still in range of the queue, it can continue
     * to process these values.
     */
    publisher(std::size_t max_queue_len, std::size_t min_queue_len = 1)
        :_q(std::make_shared<queue>(max_queue_len, min_queue_len)) {}
    
    ///Publish a value
    /**
     * @param x value to publish
     * @return
     */
    template<typename X>
    auto publish(X &&x) -> decltype(std::declval<queue>().push(std::forward<X>(x))) {
        _q->push(std::forward<X>(x));
    }
    
    ///Retrieve the queue object
    auto get_queue() {
        return _q;
    }
    
    
    ///Destructor marks the queue closed
    /**
     * When publisher is destroyed, the subscribers can still read rest of the queue.
     * However, the queue is marked as closed, so when the subscriber processes all
     * values, EOF is returned
     */
    ~publisher() {
        _q->close();
    }
    
    
    ///Closes the queue before destruction
    void close() {
        _q->close();
    }
    
    
protected:
    
    std::shared_ptr<queue> _q;
    

    
    template<typename> friend class subscriber;
};

///Subscriber, can subscribe to publisher
/**
 * @tparam T type of data to be exchanged
 */
template<typename T>
class subscriber {
public:
    
    using read_mode = typename publisher<T>::read_mode;
    
    using queue = typename publisher<T>::queue;
    ///construct subscriber
    /**
     * Subscribes and starts reading recent data
     * 
     * @param pub publisher
     * @param t type of subscription
     * 
     */
    subscriber(publisher<T> &pub, subscribtion_type t = subscribtion_type::all_values)
    :_q(pub.get_queue()),_pos(_q->subscribe(this)),_t(t) {}
    ///construct subscriber, specify starting position
    /**
     * @param pub publisher 
     * @param pos starting position
     * @param t type of subscription
     */
    subscriber(publisher<T> &pub, std::size_t pos, subscribtion_type t = subscribtion_type::all_values)
    :_q(pub.get_queue()),_pos(pos),_t(t) {
            _q->subscribe(this, _pos);
    }
    
    
    ///Subscriber can be copied
    /**
     * By copying subscriber, the copy is automatically subscribed from the position equals to
     * position of source subscriber
     *
     * @param other source subscriber
     */
    subscriber(const subscriber &other):_q(other._q),_pos(other._pos), _t(other._t){
        _q->subscribe(this, _pos);
    }
    ///can't be assigned
    subscriber &operator=(const subscriber &) = delete;
    
    
    ///Unsubscribes
    ~subscriber() {
        _q->leave(this, _pos);
    }
    
    

    ///awaits for next data
    /**
     * @return a published value
     * @exception no_longer_avaiable_exception subscriber wants to access a value, which has been outside of available queue window.
     * @exception no_more_values_exception publisher has been closed
     */
    co_awaiter<subscriber,  true> operator co_await() {
        return co_awaiter<subscriber, true>(*this);
    }

    ///Retrieves current position
    /** Position start on zero a increases for every published value. 
     * 
     * @note because first position is at zero index, reported position before the
     * first await is -1 
     * 
     * @return current position
     */
    std::size_t position() const {
        return _pos;
    }
    
protected:
    std::shared_ptr<queue> _q;
    std::size_t _pos;
    subscribtion_type _t;
    
    friend class publisher<T>;
    friend class co_awaiter<subscriber<T>, true >;

    bool is_ready() {
        return _q->advance(this, _pos,_t);
    }
    bool subscribe_awaiter(abstract_awaiter<true> *awt) {
        return _q->advance_suspend(this, _pos, awt);
    }
    std::optional<T> get_result() {
        return _q->get_value(this, _pos,_t);
    }
    

};


}



#endif /* SRC_COCLASSES_PUBLISHER_H_ */
