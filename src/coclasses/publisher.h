/**
 * @file publisher.h - publisher-subscriber pattern
 */

#pragma once
#ifndef SRC_COCLASSES_PUBLISHER_H_
#define SRC_COCLASSES_PUBLISHER_H_
#include "future.h"

#include "iterator.h"
#include <deque>
#include <map>
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
        
        using Handle = std::size_t;
        
        queue() = default;
        queue(std::size_t max_queue_len, std::size_t min_queue_len = 1)
            :_max_queue_len(max_queue_len), _min_queue_len(min_queue_len)
        {
            assert(_min_queue_len > 0);
            assert(_max_queue_len >= _min_queue_len);
        }
        ///announce position
        Handle subscribe(const subscriber<T> *sub, std::size_t pos) {
            std::lock_guard _(_mx);
            return subscribe_lk(sub,pos);
        }
        ///announce position - set to most recent
        Handle subscribe(const subscriber<T> *sub) {
            std::lock_guard _(_mx);
            return subscribe_lk(sub);
        }
        ///announce position - set to most recent
        Handle subscribe(Handle h, const subscriber<T> *sub) {
            std::lock_guard _(_mx);
            return subscribe_lk(h, sub);
        }
        bool advance(Handle id, subscribtion_type type) {
            std::lock_guard _(_mx);
            return advance_lk(id,type);
        }
        bool advance_suspend(Handle id, abstract_awaiter<> *awt) {
            std::lock_guard _(_mx);
            return advance_suspend_lk(id,awt);
        }
        
        void leave(Handle id) {
            std::lock_guard _(_mx);
            leave_lk(id);
        }
        
        std::size_t position(Handle h) {
            return _regs[h]._pos;            
        }
        
        std::optional<T> get_value(Handle id, subscribtion_type type) {
            std::lock_guard _(_mx);
            return get_value_lk(id,type);
        }
        void push(T &&val) {
            std::unique_lock<std::mutex> lk(_mx);
            _q.push_front(std::move(val));
            push_lk(lk,1);
        }
        void push(const T &val) {
            std::unique_lock<std::mutex> lk(_mx);
            _q.push_front(val);
            push_lk(lk,1);
        }
        template<typename Iter>
        void push(Iter &&from, Iter &&to) {
            std::unique_lock<std::mutex> lk(_mx);
            auto n = _q.size();
            std::copy(from, to, std::front_inserter(_q));
            auto d = _q.size() - n;
            if (d) {
                push_lk(lk,d);
            }
        }
        
        void close() {
            std::unique_lock<std::mutex> lk(_mx);
            if (_closed) [[unlikely]] return;
            _closed = true;
            push_lk(lk,0);
        }
        void kick(const subscriber<T> *sub) {
            std::unique_lock<std::mutex> lk(_mx);
            kick_lk(sub, lk);            
        }
        
    protected:
        //subscriber registration
        struct subreg_t {
            std::size_t _pos;           //reading position  (it is used as _next_free when not used)      
            const subscriber<T> *_sub;   //associated subscriber (used as identification) 
            abstract_awaiter<> *_awt;   //currently registered awaiter            
            bool _used;         //this slot is used
            bool _kicked;       //subscriber has been kicked out
        };
        
        using posreg_t = std::pair<std::size_t, Handle>; //position, index
        
        using registrations_t = std::vector<subreg_t>;          //list of registrations
        
        const std::size_t _max_queue_len = std::numeric_limits<std::size_t>::max();
        const std::size_t _min_queue_len = 1; 
        
        registrations_t _regs;  //list of registrations
        std::size_t _next_free = 0; //contains next free registration slot
        std::mutex _mx;         //mutex
        std::deque<T> _q;       //queue of items
        std::size_t _pos = 1;   //position in the stream
        std::vector<abstract_awaiter<> *> _wakeup_buffer;
        bool _closed = false;   //true if closed
        
        Handle subscribe_lk(const subscriber<T> *sub, std::size_t pos) {
            Handle h;
            if (_next_free >= _regs.size()) {
                h=_regs.size();
                _regs.push_back({pos, sub,nullptr,true,false});
                _next_free = _regs.size();
            } else {
                h = _next_free;
                subreg_t &l = _regs[_next_free];                
                _next_free = l._pos;
                l._awt = nullptr;
                l._sub = sub;
                l._pos = pos;
                l._used = true;
                l._kicked = false;
            }
            return h;
        }
        Handle subscribe_lk(const subscriber<T> *sub) {
            auto r = subscribe_lk(sub, _pos-1);
            return r;
        }
        Handle subscribe_lk(Handle h, const subscriber<T> *sub) {            
            auto r = subscribe_lk(sub, _regs[h]._pos);
            return r;
        }
        
        
        void leave_lk(Handle h) {
            subreg_t &l = _regs[h];
            assert(l._used);
            l._pos = _next_free;
            _next_free = h;
            l._used = false;
        }
        
        bool advance_lk(Handle h, subscribtion_type t) {
            subreg_t &l = _regs[h];
            if (l._kicked) return false;
            if (l._pos+1 == _pos && !_closed) return false;
            switch (t) {
                default:
                case subscribtion_type::all_values:
                    l._pos++;                    
                    break;
                case subscribtion_type::skip_if_behind:
                    l._pos = std::max(l._pos+1, _pos - _q.size());                    
                    break;
                case subscribtion_type::skip_to_recent:
                    l._pos = std::max(l._pos+1, _pos - 1);                    
                    break;
            }            
            return true;
        }

        bool advance_suspend_lk(Handle h, abstract_awaiter<> *awt) {
            subreg_t &l = _regs[h];
            if (l._kicked || _closed) return false;
            l._pos++;            
            if (l._pos == _pos) {
                l._awt = awt;
                return true;
            } else {
                return false;
            }
        }
        std::optional<T> get_value_lk(Handle h, subscribtion_type type) {
            subreg_t &l = _regs[h];
            if (l._kicked || l._pos == _pos) return {};
            switch (type) {
                default:
                case subscribtion_type::all_values: {
                    std::size_t relpos = _pos - l._pos - 1;
                    if (relpos >= _q.size()) return {};
                    return _q[relpos];
                }
                case subscribtion_type::skip_if_behind: {
                    std::size_t relpos = _pos - l._pos - 1;
                    if (relpos >= _q.size()) relpos = _q.size()-1;
                    return _q[relpos];
                }
                case subscribtion_type::skip_to_recent: {
                    return _q[0];                    
                }
            }
            
        }
        
        void push_lk(std::unique_lock<std::mutex> &lk, std::size_t count) {
             _pos+=count;
             std::size_t need_len = _min_queue_len;
             _wakeup_buffer.clear();
             for (auto &x: _regs) {
                 if (x._used) {
                     if (x._awt) {
                         _wakeup_buffer.push_back(x._awt);
                         x._awt = nullptr;
                     }
                     need_len = std::max(need_len, _pos - x._pos);
                 }
             }

             _q.resize(std::min({need_len, _max_queue_len, _q.size()}));
             auto wk = std::move(_wakeup_buffer);
             lk.unlock();
             for (abstract_awaiter<> *x: wk) x->resume();
             lk.lock();
             std::swap(wk, _wakeup_buffer);
         }
        
        void kick_lk(const subscriber<T> *sub, std::unique_lock<std::mutex> &lk) {
            abstract_awaiter<> *awt = nullptr;
            auto iter = std::find_if(_regs.begin(), _regs.end(), [&](const subreg_t &x){
               return x._used && x._sub == sub; 
            });
            if (iter != _regs.end()) {
                awt = iter->_awt;
                iter->_awt = nullptr;
                iter->_kicked =true;
            }
            lk.unlock();
            if (awt) awt->resume();
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
    
    ///Publish multiple items
    /**
     * @param beg begin iterator
     * @param end end iterator
     * 
     * @note It is faster to publish multiple items at once, than calling publish for each item,
     * because of overhead needed to handle each item. 
     */
    template<typename Iter>
    void publish(Iter &&beg, Iter &&end) {
        _q->push(std::forward<Iter>(beg), std::forward<Iter>(end));
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
    
    ///Kick given subscriber
    /**
     * @param sub pointer to subscriber to kick. The pointer is used as identification of the
     * subscriber and it is not accessed, so it can contain invalid value or point to already
     * released memory - this can handle issue with potential race condition. 
     * 
     * Kicked subscriber is waken up (if it is suspended) and receives end of stream. In
     * case if use invalid pointer, nothing happen
     */
    void kick(const subscriber<T> *sub) {
        _q->kick(sub);
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
    using iterator = generator_iterator<subscriber<T> >;
    ///construct subscriber
    /**
     * Subscribes and starts reading recent data
     * 
     * @param pub publisher
     * 
     */
    subscriber(publisher<T> &pub, subscribtion_type t = subscribtion_type::all_values)
    :_q(pub.get_queue()),_h(_q->subscribe(this)),_t(t) {}
    ///construct subscriber, specify starting position
    /**
     * @param pub publisher 
     * @param pos starting position
     */
    subscriber(publisher<T> &pub, std::size_t pos, subscribtion_type t = subscribtion_type::all_values)
    :_q(pub.get_queue()),_h(_q->subscribe(this, pos)),_t(t) {            
    }
    
    
    ///Subscriber can be copied
    /**
     * By copying subscriber, the copy is automatically subscribed from the position equals to
     * position of source subscriber
     *
     * @param other source subscriber
     */
    subscriber(const subscriber &other)
        :_q(other._q),_h(_q->subscribe(other._h, this)), _t(other._t){}
    ///can't be assigned
    subscriber &operator=(const subscriber &) = delete;
    
    
    ///Unsubscribes
    ~subscriber() {
        _q->leave(_h);
    }

    ///Awaiter for next item
    /**It allows to use next() function synchronously and asynchronously
     *
     * To access synchronously, convert result to boolean. To access
     * asynchronously, co_await to obtain boolean
     */
    class next_awt : public co_awaiter<subscriber> {
    public:
        using co_awaiter<subscriber>::co_awaiter;
        
        operator bool() {
            if (!this->await_ready()) {
                return this->wait();
            } else {
                return this->await_resume();
            }
        }
        
        bool operator !() {
            return !operator bool();
        }
    };
    

    ///awaits for next data
    /**
     * result can be converted to bool directly or through co_await. 
     *
     * @retval true next item is available. Use value() to obtain the actual value
     * @retval false stream ended, no more values are available 
     */
    next_awt next() {
        return next_awt(*this);
    }

    ///Retrieves current position
    /** Position start on zero a increases for every published value.
     * 
     * The first item has index 1, because index 0 is reserved to
     * "no items" 
     * 
     * @return current position
     */
    std::size_t position() const {
        return _q->position(_h);
    }
    
    ///Get next published item 
    /**
     * Function retrieves next item, if such item is available. 
     * 
     * @retval true next item is available, use value() to retrieve item
     * @retval false no next item is available. It doesn't indicate end
     * of the stream. You need to call next() to retrieve such a status
     */
    bool next_ready() {
        auto awt = next();
        if (!awt.await_ready()) return false;
        return awt.await_resume();
    }
    
    ///Kick this subscriber from the publisher
    /**
     * Function can be called from different thread to kick this subscriber from the
     * publisher without need to know where it is subscribed 
     * 
     */
    void kick_me() {
        _q->kick(this);
    }
    
    ///retrieve current value
    T &value() {
        return *_val;
    }

    ///retrieve current value
    const T &value() const {
        return *_val;
    }

    iterator begin() {
        return iterator(*this, next());
    }
    iterator end() {
        return iterator(*this, false);
    }
    
    
    
protected:
    using Handle = typename publisher<T>::queue::Handle;
           
    
    std::shared_ptr<queue> _q;
    Handle _h;
    subscribtion_type _t;
    std::optional<T> _val;
    
    friend class publisher<T>;
    friend class co_awaiter<subscriber<T> >;

    bool is_ready() {
        return _q->advance(_h,_t);
    }
    bool subscribe_awaiter(abstract_awaiter<> *awt) {
        return _q->advance_suspend(_h, awt);
    }
    bool get_result() {
        _val = _q->get_value(_h,_t);        
        return _val.has_value();
    }
    

};


}



#endif /* SRC_COCLASSES_PUBLISHER_H_ */
