/**
 * @file publisher.h - publisher-subscriber pattern
 */

#pragma once
#ifndef SRC_COCLASSES_PUBLISHER_H_
#define SRC_COCLASSES_PUBLISHER_H_
#include "future.h"

#include <deque>
#include <mutex>
#include <queue>

namespace cocls {

template<typename T>
class subscriber;


///Publisher
/**
 * Object allows to publish values and register subscribers
 * 
 * @tparam T type of published value
 * 
 * Publisher contains a queue, which gives subscribers chance to catch values
 * if they are slower than publisher. You can also configure minimal queue size 
 * and maximal queue size.  
 */
template<typename T>
class publisher {
public:
    
    ///construct publisher with default settings - infinite queue length
    publisher() = default;
    /**
     * construct publisher and configure it
     * 
     * @param max_queue_len specifies max queue length. Subscribers which need longer queue
     * are kicked out with exception - no_longer_available_exception;
     * 
     * @param min_queue_len specifies min queue length. By default queue is shrank to slowest
     * subscriber. This doesn't allow to 'reconnect' with ability to start reading 
     * from specified position. You can specify min queue length, which causes, that queue
     * will contain at least specified count of items, so subcriber can restart reading
     * at choosen position if the position is in min_queue_len window.
     */
    publisher(std::size_t max_queue_len, std::size_t min_queue_len = 1)
        :_min_q(std::max<std::size_t>(1, min_queue_len))
        ,_max_q(max_queue_len) {}
    
    ///Publish a value
    void publish(const T &val) {        
        std::unique_lock<std::mutex> _(_mx);
        _q.push_front(val);
        after_publish(_,1);
    }
    
    ///Publish a value
    void publish(T &&val) {        
        std::unique_lock<std::mutex> _(_mx);
        _q.push_front(std::move(val));
        after_publish(_,1);
    }

    ///Close the publisher, kick out all subscribers
    /**
    * All subscribers are kicked, even slower ones, so if there is subscriber which did not
    * read last values, it is kicked anyway.
    * 
    * If you need to close publisher graciously, use close_graciously() 
    */
    void close() {
        remove_all();
    }
    
    ///Closes the publisher graciously
    /** it is coroutine, so you need to wait before you can destroy the publisher.
     * Do not call close() or destructor while awaiting to close_graciously() 
     * 
     * @return task which can be co_awaited 
     */
    task<> close_graciously() {
        std::unique_lock<std::mutex> _(_mx);
        future<void> wt;
        _graciously_close = wt.get_promise();        
        after_publish(_,0);
        if (_subs.empty() && _graciously_close) {
            _graciously_close.set_value();
            _graciously_close.release();
        }
        _.unlock();
        co_await wt;        
    }
    
    
    ///Destroys publisher - perform close()
    ~publisher() {
        remove_all();
    }
    
protected:
    

    friend class subscriber<T>;
    
    using subslist_t = std::vector<subscriber<T> *>;
    
    
    std::mutex _mx;
    std::deque<T> _q;
    std::size_t _min_q = 1;
    std::size_t _max_q = std::numeric_limits<std::size_t>::max();
    std::size_t _msg_cntr = 0;
    subslist_t _subs;
    subslist_t _ntf;
    promise<void> _graciously_close;
    std::condition_variable *_exit_cond = nullptr;
    
    

    void after_publish(std::unique_lock<std::mutex> &lk, std::size_t inc) {
        
        subslist_t ntf;
        std::swap(_ntf,ntf);
        ntf.clear();
        std::size_t qlen = _min_q;
        
        for (subscriber<T> *r: _subs) {
            std::size_t pos = r->get_pos_internal();
            if (pos == _msg_cntr) {
                ntf.push_back(r); 
            } else {
                auto lag = _msg_cntr - pos + 1;               
                if (lag > _max_q) {                   
                    ntf.push_back(r);    
                } else {
                    qlen = std::max(lag, qlen);
                }
            }
        }
        if (qlen < _q.size()) {
            _q.resize(qlen);
        }        
        _msg_cntr+= inc;
        lk.unlock();
        for (subscriber<T> *x: ntf) {
            x->notify();
        }
        lk.lock();
        std::swap(_ntf,ntf);
    }
    
    T get_value(const std::size_t &pos) {
        std::lock_guard _(_mx);
        std::size_t relpos = _msg_cntr - pos;
        if (_exit_cond) throw no_more_values_exception();
        if (relpos-1 >= _q.size()) {
            if (_graciously_close) throw no_more_values_exception();
            else throw no_longer_avaible_exception();
        }
        return _q.at(relpos-1);
    }
    
    bool check_ready(const std::size_t &pos) {
        std::lock_guard _(_mx);
        return _graciously_close || _exit_cond || pos+1 != _msg_cntr;        
    }
    
    bool advance_and_suspend(std::size_t &pos) {
        std::lock_guard _(_mx);
        pos++;        
        return !(_graciously_close || _exit_cond || pos != _msg_cntr);
    }
    
    void reg(subscriber<T> *s, std::size_t &pos) {
        std::lock_guard _(_mx);
        _subs.push_back(s);
        pos = _msg_cntr-1;
    }
    
    void unreg(subscriber<T> *s) {
        std::lock_guard _(_mx);
        _subs.erase(std::remove(_subs.begin(), _subs.end(), s), _subs.end());
        if (_subs.empty()) {
            if (_exit_cond) _exit_cond->notify_all();
            if (_graciously_close) {
                _graciously_close.set_value();
                _graciously_close.release();
            }
        }
    }
    
    void remove_all() {
        subslist_t l;
        if (_graciously_close) {
            _graciously_close.set_value();
            _graciously_close.release();
        }
        std::unique_lock lk(_mx);
        std::condition_variable e;
        _exit_cond = &e;
        l = _subs;
        lk.unlock();
        for (subscriber<T> *s:l) {
            s->close();
        }
        lk.lock();
        e.wait(lk, [&]{return _subs.empty();});
    }    
};

///Subscriber, can subscribe to publisher
template<typename T>
class subscriber {
public:
    
    ///construct subscriber
    /**
     * Subscribes and starts reading recent data
     * 
     * @param pub publisher
     * 
     * 
     */
    subscriber(publisher<T> &pub):_pub(pub) {
        _pub.reg(this, _pos);
    }
    ///construct subscriber, specify starting position
    /**
     * @param pub publisher 
     * @param pos starting position
     */
    subscriber(publisher<T> &pub, std::size_t pos):_pub(pub), _pos(pos) {
        _pub.reg(this, pos);
    }
    
    
    ///can't be copied
    subscriber(const subscriber &) = delete;
    ///can't be assigned
    subscriber &operator=(const subscriber &) = delete;
    
    
    ///Unsubscribes
    ~subscriber() {
        std::lock_guard _(_mx);
        if (!_closed) _pub.unreg(this);
    }
    
    ///awaits for next data
    /**
     * @return a published value
     * @exception no_longer_avaiable_exception subscriber wants to access a value, which has been outside of available queue window.
     * @exception no_more_values_exception publisher has been closed
     */
    co_awaiter<subscriber> operator co_await() {
        return *this;
    }

    ///Retrieves current position
    /** Position start on zero a increases for every published value. 
     * 
     * @note because first position is at zero index, reported position before the
     * first await is -1 
     * 
     * @return current position
     */
    std::size_t get_position() const {
        std::lock_guard _(_mx);
        return _pos;
    }
    
protected:
    publisher<T> &_pub;
    std::size_t _pos;
    mutable std::mutex _mx;
    abstract_awaiter<> *_awt = nullptr;
    bool _closed = false;
    
    friend class publisher<T>;
    friend class co_awaiter<subscriber<T> >;
    bool is_ready() {
        std::lock_guard _(_mx);
        if (_closed) return true;
        if (_pub.check_ready(_pos)) {
            _pos++;
            return true;
        } else {
            return false;
        }
    }
    bool subscribe_awaiter(abstract_awaiter<> *awt) {
        std::lock_guard _(_mx);
        if (_closed) return false;
        _awt = awt;
        return _pub.advance_and_suspend(_pos);
    }
    T get_result() {
        std::lock_guard _(_mx);
        if (_closed) throw no_more_values_exception();
        return _pub.get_value(_pos);
    }
    
    std::size_t get_pos_internal() const {
        return _pos;
    }
    
    void notify() {
        abstract_awaiter<> *x = nullptr;
        {
            std::lock_guard _(_mx);
            std::swap(x, _awt);
        }
        if (x) x->resume();
    }
    
    void close() {
        abstract_awaiter<> *x = nullptr;
        {
            std::lock_guard _(_mx);
            if (!_closed) {
                _pub.unreg(this);
            }
            _closed = true;
            std::swap(x, _awt);
        }
        if (x) x->resume();
    }
    
    void check_finished(std::unique_lock<std::mutex> &lk) {
        
    }

};


}



#endif /* SRC_COCLASSES_PUBLISHER_H_ */
