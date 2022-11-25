/** @file generator.h */
#pragma once
#ifndef SRC_COCLASSES_GENERATOR_H_
#define SRC_COCLASSES_GENERATOR_H_
#include "awaiter.h"
#include "common.h"
#include "poolalloc.h"
#include "future.h"

#include "task_storage.h"
#include <memory>
#include <optional>
#include <functional>

namespace cocls {


template<typename T>
class generator_promise;

template<typename T>
class generator {
public:

    using promise_type = generator_promise<T>;

    ///Iterator
    struct iterator {
    public:
        ///construct uninitialized iterator
        iterator():_g(0),_end(true) {}
        ///construct and initialize iterator
        /**
         * @param g pointer to generator object
         * @param end set this is end iterator 
         */
        iterator(generator *g, bool end):_g(g),_end(end) {}
        ///Copy
        iterator(const iterator &other) = default;
        ///Assign
        iterator &operator=(const iterator &other) = default;
        
        ///compare iterators
        bool operator==(const iterator &other) const {
            return _g == other._g && _end == other._end;
        } 
        ///compare iterators
        bool operator!=(const iterator &other) const {
            return !operator==(other);
        } 
        
        ///access item
        T &operator *() const {return _g->_promise->get();}
        ///access item
        T *operator ->() const {return &_g->_promise->get();}
        ///move to next item
        iterator &operator++() {
            _end = !_g->_promise->next();
            return *this;
        }
        ///move to next item
        iterator operator++(int) {
            iterator r = *this;
            _end = !_g->_promise->next();
            return r;
        }
        
        
    protected:
        generator *_g;
        bool _end;
    };

    ///awaiter - allows to wait on generater using co_await
    template<typename X>
    class awaiter_t : public co_awaiter<X> {
    public:
        awaiter_t(X &x):co_awaiter<X>(x) {}
        using co_awaiter<X>::co_awaiter;
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            this->_h = h;
            this->_owner.subscribe_awaiter(this);            
            return this->_owner.get_handle();
        }
        
    protected:
        virtual void resume() noexcept override {            
            resumption_policy::unspecified<void>::resume(this->_h);
        }
    };
    
    
    ///Object returned by next() function
    /** it can be awaited by co_await, or accessed directly */
    class next_res {
    public:
        next_res(generator &owner):_owner(owner) {}
        next_res(const next_res &) = default;
        next_res &operator=(const next_res &) = delete;
        
        
        ///retrieves whether next item is available
        /**
         * @retval true next item is available
         * @retval false next item is not available
         * 
         * @note this call actually performs to move to next item when it is called for the first time
         */
        operator bool() const {
            if (!_resolved) {
                _value = _owner._promise->next();
                _resolved = true;
            }
            return _value;
        }
        ///retrieves whether generator is done
        /**
         * @retval true generator done
         * @return false generator is not done yet
         */
        bool operator !() const {
            return !operator bool();
        }
        ///await this state
        /**
         * @retval true next item loaded
         * @retval false generator is done
         */
        awaiter_t<next_res> operator co_await() {
            return *this;
        }
        
        
    protected:
        mutable bool _resolved = false;
        mutable bool _value = false;
        generator &_owner;
        
        bool is_ready() {
            return false;
        }
        bool subscribe_awaiter(abstract_awaiter<> *awt) {
            return _owner._promise->subscribe_awaiter(awt);
        }
        bool get_result() {
            return _owner._promise->check_ready();
        }
        std::coroutine_handle<> get_handle() {
            return std::coroutine_handle<promise_type>::from_promise(*_owner._promise);
        }
        
        template<class> friend class awaiter_t;
        friend class co_awaiter<next_res>;
        
    };

    
    ///construct empty generator variable - can be assigned
    generator() = default;
    generator(promise_type *promise):_promise(promise) {}

    ///Retrieve ID of this coroutine
    coro_id get_id() {
        return std::coroutine_handle<promise_type>::from_promise(*_promise).address();
    }
    ///Returns iterator
    /**
     * Despite on name, this function loads the first item and returns iterator. Further calls
     * loads more and more items Actually there is no separate iterators. The iterator only
     * allows to iterate generated items through the ranged-for (simulation)
     *
     */
    iterator begin() {
        return iterator(this, !_promise->next());
    }
    ///Returns iterator which represents end of iteration
    iterator end() {
        return iterator(this, true);
    }
    ///Runs generator and waits to generation of next item
    /**
     * @return co_awaitable object. If called from non-coroutine, you need to convert returned
     * object to bool to perform loading of next item and returning true/false whether the
     * next item is available
     * 
     * @code
     * if (generator.next()) {
     *      //next item is loaded
     * } else {
     *      //generator is done
     * }
     * @endcode
     * 
     */
    next_res next() {
        return *this;
    }
    ///Retrieves current value
    /**
     * @return current value
     * 
     * @note you need to call next() or check for done()
     */
    T &value() {
        return _promise->get();
    }
    ///Retrieves current value
    /**
     * @return current value
     * 
     * @note you need to call next() or check for done()
     */
    const T &value() const {
        return _promise->get();
    }

    ///Run generator and retrieve next item
    /**
     * @return if the next item is available, function returns optional object with value.
     * If the generator is done, returns empty value
     * 
     * @note function is synchronous. If the generator is asynchronous, function blocks current
     * thread until the generator is done. If you need co_await the generator, just call co_await on
     * its object directly
     * 
     */
    std::optional<T> operator()() {
        if (_promise->next()) {
            return std::optional<T>(std::move(_promise->get()));
        } else {
            return {};
        }
    }
    
    ///Checks, whether 
    bool done() const {
        return _promise->done();
    }
    

    ///Allows to co_await on generator
    /**
     * @return std::optional<T> for next item, similar to operator()
     */
    awaiter_t<promise_type> operator co_await() {
        return *_promise;
    }
    
    
    ///request next item. Callback is called when item is ready
    /**
     * @param fn callback function. Function has no arguments. To retrieve item, use
     * done() to check, whether generator is done and if not, use value()
     * 
     * @note do not use if previous value was not processed yet
     */
    template<typename Fn>
    auto operator>>(Fn &&fn) -> decltype(fn(), std::terminate()) {
        _promise->next_cb(std::forward<Fn>(fn));
    }
    
protected:

    struct Deleter {
    public:
        void operator()(promise_type *p) {
            p->destroy();
        }
    };
    
    std::unique_ptr<promise_type, Deleter> _promise;    
    
    promise_type *get_promise() const {return _promise.get();}
    
};

template<typename T>
class generator_promise: public coro_allocator {
public:

    ///Generator's state
    enum class State {
        ///generator was not started
        not_started,
        ///generator is running, new value is not ready yet
        running,
        ///generator is running, but promise object is prepared to accept new state
        running_promise_set,
        ///data are ready, can be read
        data_ready,
        ///generator finished, no more data
        done
    };

    
    generator<T> get_return_object() {
        return generator<T>(this);
    }
    static std::suspend_always initial_suspend() noexcept {return {};}
    struct yield_suspender {
        yield_suspender(abstract_awaiter<> *h): _h(h) {}
        bool await_ready() const noexcept {return false;}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> ) const noexcept {
            if (_h) return _h->resume_handle();
            else return std::noop_coroutine();
        }
        void await_resume() const noexcept {};

        abstract_awaiter<> *_h;
    };
    
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

    yield_suspender yield_value(T &value) noexcept {
        _value = &value;
        return yield_cont();
    }
    yield_suspender yield_value(T &&value) noexcept {
        _value = &value;
        return yield_cont();
    }

    T &get() {        
        if (_exception) {
            _state = State::done;
            std::rethrow_exception(_exception);
        }
        if (_state == State::data_ready) {
            return *_value;       
        }
        throw value_not_ready_exception();
    }
    
    bool done() const {
        return _state == State::done;
    }
    
    bool next() {
        auto h = std::coroutine_handle<generator_promise>::from_promise(*this);
        if (_state.load(std::memory_order_relaxed) == State::done) return false;
        subscribe_awaiter(nullptr);
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
        return _state == State::data_ready;
    }
        
    template<typename Fn>
    void next_cb(Fn &&fn) {        
          auto h = std::coroutine_handle<generator_promise>::from_promise(*this);
          if (_state.load(std::memory_order_relaxed) == State::done) {
              fn();
              return;
          }
          subscribe_awaiter(nullptr);
          h.resume();
          if (_state.load(std::memory_order_relaxed) == State::running) {
              future<State> f;
              State chk = State::running;
              _wait_promise = cocls::make_promise<State>([this, fn = std::forward<Fn>(fn)](cocls::future<State> &f) mutable {
                  try {
                      _state = f.get();
                  } catch (...) {
                      _state = State::done;
                  }
                  fn();
              }, _future_storage);
              if (!_state.compare_exchange_strong(chk, State::running_promise_set, std::memory_order_release)) {
                  _wait_promise.set_value(chk);
                  _wait_promise.release();
              }            
              return;
          }
          fn();
    }
    
    bool subscribe_awaiter(abstract_awaiter<> *h) {
        assert(_state != State::running);
        assert(_state != State::running_promise_set);

        _awaiter = h;
        _value = nullptr;
        _state = State::running;
        return true;
    }

    void destroy() {
        assert(_state != State::running);
        assert(_state != State::running_promise_set);
        auto h = std::coroutine_handle<generator_promise>::from_promise(*this);        
        h.destroy();
    }
    
    void unhandled_exception() {
        _exception = std::current_exception();
        yield_cont();
        
    }
    
    bool is_ready() {
        return false;
    }
    
    bool check_ready() {
        if (_state == State::done) return false;
        assert(_state != State::running);
        assert(_state != State::running_promise_set);
        return true;
    }
    
    std::optional<T> get_result() {
        if (_state == State::done) return {};
        assert(_state != State::running);
        assert(_state != State::running_promise_set);
        return std::move(get());
        
    }
    
    void return_void() {
        
    }
    
    friend class co_awaiter<generator_promise>;

    std::coroutine_handle<> get_handle() {
        return std::coroutine_handle<generator_promise>::from_promise(*this);
    }

protected:
    //contain state
    std::atomic<State>  _state = State::not_started;
    //contains recorded exception
    std::exception_ptr _exception;
    //contains current value
    T *_value = nullptr;
    //contains consumer's handle - this coroutine must be resumed on co_yield
    abstract_awaiter<> *_awaiter = nullptr;
    //contains promise when State::promise_set is active
    promise<State> _wait_promise;
    //storage for future
    reusable_task_storage _future_storage;

};



///Not actual generator, just an interface
/**
 * @tparam T type of generator
 * 
 * stoppable generator can be stopped anytime from outside. Result of stopping could
 * be marking generator finished.
 * 
 * This is better solution than force destroying the generator, because you can stop
 * it when it is pending.
 * 
 * Using this interface is simple. The backend should create two functions. The first
 * function is actual generator while other function is function which starts the generator
 * passing a stopping singaling object into the generator. Then it pack generator
 * and stop function into instance of this object
 * 
 */
template<typename T>
class stoppable_generator: public generator<T> {
public:
    using stop_fn = std::function<void()>;
    stop_fn stop;
    stoppable_generator(generator<T> &&gen, stop_fn &&fn)
        :generator<T>(std::move(gen))
        ,stop(std::move(fn)) {}    
    stoppable_generator(stoppable_generator &&) = default;
    stoppable_generator(const stoppable_generator &) = delete;
};

}



#endif /* SRC_COCLASSES_GENERATOR_H_ */
