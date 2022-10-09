#pragma once

#ifndef SRC_COCLASSES_GENERATOR_H_
#define SRC_COCLASSES_GENERATOR_H_

#include "common.h" 
#include "exceptions.h"

#include "queue.h"

#include "coroid.h"

#include <optional>
#include <future>

namespace cocls {

template<typename T>
class generator_promise;

template<typename T> 
class generator_multi_awaiter;

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
    next_awaiter operator co_await() const {
        return next_awaiter(*_prom);
    }
    
    bool ready() const {
        return _prom->next_ready();
    }
    
    template<typename FN>
    void next(FN &&fn) {
        _prom->next_cb(std::forward<FN>(fn));
    }
    
    
    coroid_t get_id() const {
        return coroid_t(std::coroutine_handle<promise_type>::from_promise(*_prom));
    }

protected:

    
    friend class generator_multi_awaiter<T>;
    
    std::unique_ptr<promise_type, Deleter> _prom;    
    
    promise_type *get_promise() const {return _prom.get();}
    
};

template<typename T>
class generator_promise {
public:
    
    enum class State {
        //generator was not started
        not_started,
        //generator is running, new value is not ready yet
        running,
        //generator is running, but promise object is prepared to accept new state
        running_promise_set,
        //data are ready, can be read
        data_ready,
        //data are ready, was read
        data_processed,
        //generator finished, no more data
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

    /*
     * checks if promise set, if does, resolves it, with status done,
     * otherwise, set status done
     * finally resumes consumer if there is any
     */
    yield_suspender final_suspend() noexcept {
        State chk = _state.exchange(State::done, std::memory_order_acquire);
        if (chk == State::running_promise_set) {
            _wait_promise.set_value(State::done);
            _wait_promise.release();
        }
       
        auto h = _awaiter;
        _awaiter = nullptr;
        return yield_suspender(h);
    }


    /*
     * checks if promise is set - if does, resolves it, which status data_ready
     * otherwise sets data_ready
     * finally resumes consumer if there is any
     */
    yield_suspender yield_cont() noexcept {
        State chk = _state.exchange(State::data_ready, std::memory_order_acquire);
        if (chk == State::running_promise_set) {
            _wait_promise.set_value(State::data_ready);
            _wait_promise.release();
        }
        auto h = _awaiter;
        _awaiter = nullptr;
        return yield_suspender(h);
    }

    //on co_yield
    yield_suspender yield_value(T &&value) noexcept {
        _value = std::move(value);
        return yield_cont();
    }
    //on co_yield
    yield_suspender yield_value(const T &value) noexcept {
        _value = value;
        return yield_cont();
    }
    
    
    //called by co_await from generator
    /*
     * checks whether state is done, or data are ready. In this state, no suspend is needed
     */
    bool next_ready() {
        return _state == State::done || _state == State::data_ready;
    }
    
    //called by co_await from generator on suspend
    /*
     * records consumer,
     * resets values
     * sets state running - this allows to detect state after return from resume
     * 
     */
    void next_suspend(std::coroutine_handle<> h) {
        _awaiter = h;
        _value.reset();
        _state = State::running;
    }
    
    //called by co_await from generator on await_resume
    /*
     * just returns true, if state differs from done (State::data_ready or State::done is only valid there)
     
     */
    bool next_resume() {
        assert(_state == State::data_ready || _state == State::done);
        return _state != State::done;
    }       
    
    //called from generator by function next()
    /*
     * This called by normal function - not coroutine, so no co_await is available.
     * Generator will run on current stack. It can use co_await, however, if the execution
     * is transfered to different thread, h.resume() returns while state is set to State::running
     * 
     * Note in this case, we are in MT environment, co_yield can be called from different thread.
     * So State::running is not definitive.
     * 
     * To handle this situation, we set future<State> and promise<State>. Then we try to
     * change State from State::running to State::promise_set (with release, to transfer new
     * state of promise to the other thread). If this fails, the State changed, so we no longer
     * need promise and it is released. If we successfuly set State to State::promise_set, the
     * other thread is forced to resolve the promise. Just wait for the future and retrieve 
     * new state from it
     * 
     * Summary: Accessing async generator from non-coroutine causes blocking call even if the
     * execution is transfered to different thread. Current thread remains blocked until the
     * value is calculated
     * 
     */
    bool next() {        
        auto h = std::coroutine_handle<generator_promise>::from_promise(*this);
        if (_state.load(std::memory_order_relaxed) == State::done) return false;
        next_suspend(std::noop_coroutine());
        h.resume();
        if (_state.load(std::memory_order_relaxed) == State::running) {
            future<State> f;
            State chk = State::running;
            _wait_promise = f.get_promise();
            if (_state.compare_exchange_strong(chk, State::running_promise_set, std::memory_order_release)) {
                _state = f.wait();                
            } else {
                _wait_promise.release();
            }            
        }
        return next_resume();
    }
    

    template<typename Fn>
    void next_cb(Fn &&fn) {        
        auto h = std::coroutine_handle<generator_promise>::from_promise(*this);
        if (_state.load(std::memory_order_relaxed) == State::done) {
            fn(false);
            return;
        }
        next_suspend(std::noop_coroutine());
        h.resume();
        if (_state.load(std::memory_order_relaxed) == State::running) {
            future<State> f;
            State chk = State::running;
            _wait_promise = cocls::make_promise<State>([this, fn = std::forward<Fn>(fn)](cocls::future<State> &f) mutable {
                _state = f.get();
                fn(next_resume());
            }, _storage, sizeof(_storage));
            if (!_state.compare_exchange_strong(chk, State::running_promise_set, std::memory_order_release)) {
                _wait_promise.set_value(chk);
                _wait_promise.release();
            }            
            return;
        }
        fn(next_resume()); 
    }
    
    void return_void() {}

    //Called by Deleter
    void destroy() {
        std::coroutine_handle<generator_promise>::from_promise(*this).destroy();
    }
        
    //Called from generator to obtain value
    /* If State::data_ready || State::data_processed, state is set to State::data_processed. It
     * also use acquire to synchronize result to this thread. Other state are resolved as exception.
     * Exception state can be active with state State::done
     */
    T &get() {
        State v = State::data_ready;
       if (_state.compare_exchange_strong(v, State::data_processed, std::memory_order_acquire) || v == State::data_processed) {
           if (_exception != nullptr) std::rethrow_exception(_exception);
           else return *_value;
       } else if (v == State::done){
           if (_exception != nullptr) std::rethrow_exception(_exception);
           throw no_more_values_exception();
       } else {
           throw value_not_ready_exception();
       }
    }
    
    //calls next() but not if data_ready
    bool next_if_processed() {
        if (_state == State::data_ready) return true;
        return next();
    }
    
    //called when unhandled exception
    void unhandled_exception() {
        _exception = std::current_exception();        
    }
    
    bool can_continue() const {
        return _state == State::data_processed || _state == State::not_started ;
    }


    
protected:
    //contain state
    std::atomic<State>  _state = State::not_started;
    //contains recorded exception
    std::exception_ptr _exception;
    //contains current value
    std::optional<T> _value;
    //contains consumer's handle - this coroutine must be resumed on co_yield
    std::coroutine_handle<> _awaiter;
    //contains promise when State::promise_set is active
    promise<State> _wait_promise;
    //stortage for callback future
    char _storage[24*sizeof(void *)];
};


///Aggregator of multiple generators
/**
 * @param list__ list of generators to aggregate. The vector is passed 
 * as rvalue reference to avoid copying (because generators are movable)_
 * @return generator
 */
#if 0
template<typename T>
generator<T> generator_aggregator(std::vector<generator<T> > &&list__) {
    std::vector<generator<T> > list(std::move(list__));
    aggregator<generator<T> *, bool> aggr;
    for (auto &x: list) {
        x.next(aggr.make_callback(&x));
    }
    while (!aggr.empty()) {
        auto kv = co_await aggr;
        if (kv.second) {
            co_yield kv.first->get();
            kv.first->next(aggr.make_callback(kv.first));
        }
    }
    
}
#endif




template<typename T>
generator<T> generator_aggregator(std::vector<generator<T> > list__) {
    std::vector<generator<T> > list(std::move(list__));
    queue<std::pair<generator<T> *, bool> > queue;
    int running = list.size();;
    for (auto &x: list) {
        x.next([gen = &x, &queue](bool v){
            queue.push({gen, v});
        });
    }
    while (running) {
        std::pair<generator<T> *, bool>  kv = co_await queue.pop();;
        if (kv.second) {            
            co_yield kv.first->get();
            kv.first->next([gen = kv.first, &queue](bool v){
                queue.push({gen, v});
            });            
        } else {
            running--;
        }
    }
    
}

}



#endif /* SRC_COCLASSES_GENERATOR_H_ */
