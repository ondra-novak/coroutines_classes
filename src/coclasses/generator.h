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
 * 
 * Generator can be movable but not copied. Dropping instance causes that generator is terminated
 * 
 * Generator is started in "lazy" mode, first access starts generator until first co_yield. 
 * 
 * @note generator is executed when next(). Accessing value doesn't execute generator. You must call next()
 * for the first time to access next value. The same operation can handle operator ! and operator bool. There
 * is only one difference, they will not advance position until data are processed
 *
 * Generator function can use co_await. If such generator is called from non-coroutine, accessing items 
 * are done by blocking call. If such generator is called from coroutine, coroutine symmetric transfer is used.
 * 
 * 
 * 
 * 
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
    
    ///Iterator
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
    
    
    ///Advance to next item. 
    /**
     * Must be called as first operation (operator ! and operator bool and co_await can be used instead as well)
     * Function always advances to next item
     * 
     * @retval true next item is ready
     * @retval false no more items
     * 
     * @note function invalidates any returned reference
     */
    bool next() {
        return _prom->next();
    }
    
    ///Retrieve iterator
    iterator begin() {
        return iterator(this, !_prom->next());
    }
    ///Retrieve end iterator
    iterator end() {
        return iterator(this, true);
    }
    
    ///Determines, whether there are more items available
    /**
     * Function advances to next item when previous item has been retrieved
     * @retval true no more values
     * @retval false more values
     * 
     * @note do not combine with iterators. 
     */
    bool operator! () {
        return !_prom->next_if_processed();
    }
    ///Determines, whether there are more items available
    /**
     * Function advances to next item when previous item has been retrieved
     * @retval true no more values
     * @retval false more values
     * 
     * @note do not combine with iterators. 
     */
    operator bool () {
        return _prom->next_if_processed();
    }
    
    
    ///Retrieve current item
    /**     
     * @return reference to current item
     * @note calling this function marks item retrieved. This doesn't advance position until operator !, 
     * operator bool or co_await is used.
     */
    T &get() {
        return _prom->get();
    }
    ///Retrieve current item
    /**     
     * @return reference to current item
     * @note calling this function marks item retrieved. This doesn't advance position until operator !, 
     * operator bool or co_await is used.
     */
    T &operator()() {
        return _prom->get();
    }

    class next_awaiter {
    public:
        next_awaiter(promise_type &owner):_owner(owner) {}
        next_awaiter(const next_awaiter &other) = default;
        next_awaiter &operator=(const next_awaiter &other) = delete;
                ;
        bool await_ready() const {return _owner.next_ready();}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            _owner.next_suspend(h);
            return std::coroutine_handle<promise_type>::from_promise(_owner);
        }
        bool await_resume() const {
            return _owner.next_resume();
        }
        
    protected:
        promise_type &_owner;
        
    };
    
    ///Allows to wait on generator in coroutine using co_await
    /**
     * You need to await to calculate next item. Result is bool
     * @retval true next item has been calculated
     * @retval false no more items
     * 
     * @code
     * generator<int> g = call_gen();
     * while(co_await g) {
     *          std::cout << "value: " << g() << std::endl;
     * }
     * @endcode
     * 
     */
    next_awaiter operator co_await() {
        return next_awaiter(*_prom);
    }
    
    
protected:
    
    std::unique_ptr<promise_type, Deleter> _prom;    
    
};

template<typename T>
class generator_promise {
public:
    
    enum class State {
        not_started,
        running,
        promise_set,
        data_ready,
        data_processed,
        done
    };
    
    generator<T> get_return_object() {
        return generator<T>(this);
    }
    static std::suspend_always initial_suspend() noexcept {return {};}

    struct yield_suspender {
        yield_suspender(std::coroutine_handle<> h): _h(h) {}
        bool await_ready() const noexcept {return false;}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> ) const noexcept {
            return _h?_h:std::noop_coroutine();
        }
        void await_resume() const noexcept {};

        std::coroutine_handle<> _h;
    };

    yield_suspender final_suspend() noexcept {
        State chk = _state.exchange(State::done, std::memory_order_acquire);
        if (chk == State::promise_set) {
            _wait_promise.set_value(State::done);
            _wait_promise.release();
        }
       
        auto h = _awaiter;
        _awaiter = nullptr;
        return yield_suspender(h);
    }


    yield_suspender yield_cont() noexcept {
        State chk = _state.exchange(State::data_ready, std::memory_order_acquire);
        if (chk == State::promise_set) {
            _wait_promise.set_value(State::data_ready);
            _wait_promise.release();
        }
        auto h = _awaiter;
        _awaiter = nullptr;
        return yield_suspender(h);
    }

    yield_suspender yield_value(T &&value) noexcept {
        _value = std::move(value);
        return yield_cont();
    }
    yield_suspender yield_value(const T &value) noexcept {
        _value = value;
        return yield_cont();
    }
    
    
    bool next_ready() {
        return _state == State::done || _state == State::data_ready;
    }
    
    void next_suspend(std::coroutine_handle<> h) {
        _awaiter = h;
        _value.reset();
        _state = State::running;
    }
    
    bool next_resume() {
        return _state != State::done;
    }       
    
    bool next() {        
        auto h = std::coroutine_handle<generator_promise>::from_promise(*this);
        if (_state.load(std::memory_order_relaxed) == State::done) return false;
        next_suspend(std::noop_coroutine());
        h.resume();
        if (_state.load(std::memory_order_relaxed) == State::running) {
            future<State> f;
            State chk = State::running;
            _wait_promise = f.get_promise();
            if (_state.compare_exchange_strong(chk, State::promise_set, std::memory_order_release)) {
                _state = f.wait();                
            } else {
                _wait_promise.release();
            }            
        }
        return next_resume();
    }
    
    
    bool is_done() {
        return std::coroutine_handle<generator_promise>::from_promise(*this).done();
    }
    void destroy() {
        std::coroutine_handle<generator_promise>::from_promise(*this).destroy();
    }
        
    T &get() {
        State v = State::data_ready;
       if (_state.compare_exchange_strong(v, State::data_processed, std::memory_order_acquire)) {
           if (_exception != nullptr) std::rethrow_exception(_exception);
           else return *_value;
       } else {
           throw value_not_ready_exception();
       }
    }
    
    bool next_if_processed() {
        if (_state == State::data_ready) return true;
        return next();
    }
    
   

    void unhandled_exception() {
        _exception = std::current_exception();        
    }
    
protected:
    std::atomic<State>  _state = State::not_started;
    std::exception_ptr _exception;
    std::optional<T> _value;
    std::coroutine_handle<> _awaiter;
    promise<State> _wait_promise;
};


}



#endif /* SRC_COCLASSES_GENERATOR_H_ */
