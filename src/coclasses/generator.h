#pragma once

#ifndef SRC_COCLASSES_GENERATOR_H_
#define SRC_COCLASSES_GENERATOR_H_

#include "common.h" 
#include "exceptions.h"

#include <optional>
#include <future>

namespace cocls {

template<typename T>
class generator_promise;

///Coroutine generator
/**
 * @tparam T type of generated value
 */
template<typename T>
class generator {
public:

    using promise_type = generator_promise<T>;

    struct Deleter {
    public:
        void operator()(promise_type *p) {
            p->destroy();
        }
    };
        
    generator(promise_type *prom):_prom(prom) {}
    
    struct iterator {
    public:
        iterator():_g(0),_end(true) {}
        iterator(generator *g, bool end):_g(g),_end(end) {}
        iterator(const iterator &other) = default;
        iterator &operator=(const iterator &other) = default;
        
        bool operator==(const iterator &other) const {
            return _g == other._g && _end == other._end;
        } 
        bool operator!=(const iterator &other) const {
            return !operator==(other);
        } 
        
        T &operator *() const {return _g->_prom->get();}
        T *operator ->() const {return &_g->_prom->get();}
        iterator &operator++() {
            _end = !_g->_prom->next();
            return *this;
        }
        iterator operator++(int) {
            iterator r = *this;
            _end = !_g->_prom->next();
            return r;
        }
        
        
    protected:
        generator *_g;
        bool _end;
    };
    
    
    ///Retrieve iterator
    iterator begin() {
        return iterator(this, !_prom->next());
    }
    ///Retrieve end iterator
    iterator end() {
        return iterator(this, true);
    }
    
    ///Test whether no more values
    /**
     * @retval true no more values
     * @retval false more values
     * 
     * @note do not combine with iterators. 
     */
    bool operator! () {
        return !_prom->is_done_prefetch();
    }
    ///Test whether there are mo values
    /**
     * @retval false no more values
     * @retval true more values
     * 
     * @note do not combine with iterators. 
     */
    operator bool () {
        return _prom->is_done_prefetch();
    }
    
    ///Retrieve next value
    T &operator()() {
        return _prom->get_prefetch();
    }

    auto operator co_await() {
        return _prom->get_future_prefetch().operator co_await();
    }
    
    
protected:
    
    std::unique_ptr<promise_type, Deleter> _prom;    
    
};

template<typename T>
class generator_promise {
public:
    generator<T> get_return_object() {
        return generator<T>(this);
    }
    static std::suspend_always initial_suspend() noexcept {return {};}
    static std::suspend_always final_suspend() noexcept {return {};}

    struct yield_suspender {
        yield_suspender(bool suspend, std::coroutine_handle<> h)
            :_s(suspend),_h(h) {}
        bool await_ready() const {return !_s;}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> ) const {
            if (_h) {
                return _h;
            }
            else {
                return std::noop_coroutine();
            }
        }
        void await_resume() const {};

        bool _s;
        std::coroutine_handle<> _h;
    };


    yield_suspender yield_cont() noexcept {
        std::coroutine_handle<> h;
        bool ok = _promise->release(h);
        if (ok) return yield_suspender(true, h);
        else return yield_suspender(true, nullptr);
    }

    yield_suspender yield_value(T &&value) noexcept {
        _promise->set_value(std::move(value));
        return yield_cont();
    }
    yield_suspender yield_value(const T &value) noexcept {
        _promise->set_value(value);
        return yield_cont();
    }
    
    bool is_done()  {
        return std::coroutine_handle<generator_promise>::from_promise(*this).done();
    }
    void destroy() {
        std::coroutine_handle<generator_promise>::from_promise(*this).destroy();
    }
    
    bool next() {
        auto h = std::coroutine_handle<generator_promise>::from_promise(*this);
        if (h.done()) return false;
        _value.reset();
        _value.emplace();
        _promise = _value->get_promise();
        h.resume();
        return (!h.done());
    }
    
    T &get() {
        if (_value.has_value()) return _value->wait();
        else throw value_not_ready_exception();        
    }
    
    future<T> &get_future() {
        if (_value.has_value()) return *_value;
        else throw value_not_ready_exception();
    }

    bool is_done_prefetch() {
        if (!_prefetch) {
            _prefetch = true;
            return next();
        }
        else return is_done();
    }
    
    T &get_prefetch() {
        if (!_prefetch) {
            if (!next()) throw no_more_values_exception();            
        }
        T &out = get();
        _prefetch = false;
        return out;
        
    }

    future<T> &get_future_prefetch() {
        if (!_prefetch) {
            if (!next()) throw no_more_values_exception();
        }
        future<T> &out = get_future();
        _prefetch = false;
        return out;

    }



    void unhandled_exception() {
        _value->get_promise().unhandled_exception();
    }
    
protected:
    std::optional<future<T> > _value;
    std::optional<promise<T> > _promise;
    bool _prefetch = false;
};


}



#endif /* SRC_COCLASSES_GENERATOR_H_ */
