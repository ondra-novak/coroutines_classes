#pragma once

#ifndef SRC_COCLASSES_GENERATOR_H_
#define SRC_COCLASSES_GENERATOR_H_

#include "common.h" 
#include "exceptions.h"

#include <optional>
#include <coroutine>
#include <memory>

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
    std::suspend_always yield_value(T &&value) noexcept {
        _value = std::move(value);
        _exception = nullptr;
       return {};
    }
    std::suspend_always yield_value(const T &value) noexcept {
        _value = value;
        _exception = nullptr;
       return {};
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
        h.resume();
        return (!h.done());
    }
    
    T &get() {
        if (_exception) std::rethrow_exception(_exception);
        else if (_value.has_value()) return *_value;
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
    
    // Disallow co_await in generator coroutines.
    void await_transform() = delete;           
    void unhandled_exception() {
       _value.reset();
       _exception = std::current_exception();
    }
    
protected:
    std::optional<T> _value;
    std::exception_ptr _exception;
    bool _prefetch = false;
};


}



#endif /* SRC_COCLASSES_GENERATOR_H_ */
