#pragma once
#ifndef SRC_COCLASSES_GENERATOR_H_
#define SRC_COCLASSES_GENERATOR_H_
#include "awaiter.h"

#include "co_alloc.h"

#include "coro_storage.h"

#include "iterator.h"

#include <memory>
#include <optional>

namespace cocls {


template<typename T>
class generator_promise;

///Generic generator
/**
 * @tparam T type to be generated
 * 
 * 
 * Generator supports both synchronous and asynchronous coroutine for generating,
 * and synchronous and asynchronous access from the reader
 * 
 * - generating routine can use co_await
 * - the generator itself can be co_awaited
 * 
 * Accessing generator synchronously while it itself running asynchronously is
 * provided via a condition variable, so accessing thread is blocked. It is
 * much better to use co_await to access asynchronous generator.
 * 
 * It is possible to co_await synchronous generator, which uses symmetric transfer
 * between reading and generating coroutine
 * 
 * It is possible to use ranged-for to read generated values, however, this
 * way is always synchronous. 
 * 
 * To asynchronously access the generator, you need to co_await result of method
 * next(), which returns true if there is next item, or false if generation is done
 * 
 * 
 * @code
 * generator<int> gen = ...;
 * while (co_await gen.next()) {
 *      int v = gen.value();
 * }
 * @endcode
 * 
 * Above code can be used for synchronous access (without co_await)
 * 
 * @code
 * generator<int> gen = ...;
 * while (gen.next()) {
 *      int v = gen.value();
 * }
 * @endcode
 * 
 * For asynchronous access you can use next_cb to assign a callback which
 * is called when generation is complete. This feature is used by generator_aggregator
 * 
 * 
 
 */
template<typename T>
class generator {
public:

    using promise_type = generator_promise<T>;
  

    using iterator = generator_iterator<generator<T> >;
    
    ///Object returned by next() function
    /** it can be awaited by co_await, or accessed directly 
     * 
     * when accessed directly, it acts as boolean, which contains true 
     * if there is next item, or false if not. Note accessing this 
     * virtual value performs getting next item every time the value is
     * retrieved, So it is not recommended to store object anywhere, just
     * immediately convert that temporary object to bool
     * 
     * @code 
     *  while (gen.next()) { //synchronous access
     *          // work with gen.value()
     *  }
     * @endcode
     *      
     * @code 
     *  while (co_await gen.next()) {  //asynchronous access
     *          // work with gen.value()
     *  }
     * @endcode
     * 
     *  
     * */
    class next_awaiter: public abstract_awaiter<false> {
    public:
        next_awaiter(generator &owner):_owner(owner) {}
        next_awaiter(const next_awaiter &) = default;
        next_awaiter &operator=(const next_awaiter &) = delete;
        
        
        ///retrieves whether next item is available
        /**
         * @retval true next item is available
         * @retval false next item is not available
         */
        operator bool() const {
            return _owner._promise->next();
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

        
        bool await_ready() noexcept { 
            return _owner._promise->done();
        }
        
        bool await_resume() {
            return _owner._promise->on_await_resume();
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            _h = h;
            return _owner._promise->next_async(this);
        }


    protected:
        generator &_owner;
        std::coroutine_handle<> _h;
        
        virtual void resume() noexcept override {
            _h.resume();
        }
        virtual std::coroutine_handle<void> resume_handle() override {
            return _h;
        }
        
        
        
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
        return iterator(*this, next());
    }
    ///Returns iterator which represents end of iteration
    iterator end() {
        return iterator(*this, false);
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
    next_awaiter next() {
        return *this;
    }
    ///Retrieves current value
    /**
     * @return current value
     * 
     * @note you need to call next() or check for done()
     */
    T &value() {
        return *_promise->get();
    }
    ///Retrieves current value
    /**
     * @return current value
     * 
     * @note you need to call next() or check for done()
     */
    const T &value() const {
        return *_promise->get();
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
            return std::optional<T>(std::move(*_promise->get()));
        } else {
            return {};
        }
    }
    
    ///Checks, whether 
    bool done() const {
        return _promise->done();
    }
    
    
    ///request next item. Callback is called when item is ready
    /**
     * @param fn callback function. Function has no arguments. To retrieve item, use
     * done() to check, whether generator is done and if not, use value()
     * 
     *  
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
    generator_promise() = default;
    generator_promise(const generator_promise &) = delete;
    generator_promise &operator=(const generator_promise &) = delete;
    
    using awaiter = abstract_awaiter<false>;

    class awaiter_base: public awaiter {
    public:
        
        void *operator new(std::size_t sz, reusable_storage &stor) {
            return stor.alloc(sz);
        }
        void operator delete(void *ptr, reusable_storage &stor) {}
        void operator delete(void *ptr, std::size_t) {}        
    };
    
    class syncing_awaiter: public awaiter_base {
    public:
        virtual void resume() noexcept override {
            {
            std::lock_guard _(_mx);
            _ready = true;
            }
            _cvar.notify_all();
        }
        virtual std::coroutine_handle<> resume_handle() override {
            resume();
            return std::noop_coroutine();
        }
        void wait() {
            std::unique_lock lk(_mx);
            _cvar.wait(lk, [&]{return _ready;});
        }

    protected:
        bool _ready = false;
        std::mutex _mx;
        std::condition_variable _cvar;
    };
    
    template<typename Fn>
    class callback_awaiter: public awaiter_base { // @suppress("Miss copy constructor or assignment operator")
    public:
        callback_awaiter(generator_promise &owner, Fn &&fn):_owner(owner)
            ,_fn(std::forward<Fn>(fn)) {}
        virtual void resume() noexcept override {
            auto fn(std::move(_fn)); //transfer function to the stack frame, because object will be destroyed 
            _owner.after_callback_resume(this); 
            delete this;
            fn();
            
        }
        virtual std::coroutine_handle<> resume_handle() override {
            resume();
            return std::noop_coroutine();
        }
        generator_promise &_owner;
        Fn _fn;    
        
    };
    
    void destroy() {
        auto h = std::coroutine_handle<generator_promise<T> >::from_promise(*this);
        h.destroy();
    }
    
    static std::suspend_always initial_suspend() noexcept {return {};}
    struct yield_suspender { // @suppress("Miss copy constructor or assignment operator")
        abstract_awaiter<> *_h;

        bool await_ready() const noexcept {return false;}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> ) const noexcept {
            if (_h) {
                return _h->resume_handle();
            } else {
                return std::noop_coroutine();
            }
        }
        void await_resume() const noexcept {};
    };

    yield_suspender final_suspend() noexcept {
        _value = nullptr;
        return yield_suspender{_awaiter};
    }
    
    void unhandled_exception() {
        _e= std::current_exception();
    }

    yield_suspender yield_value(T &value) noexcept {
        _value = &value;
        return yield_suspender{_awaiter};
    }
    yield_suspender yield_value(T &&value) noexcept {
        _value = &value;
        return yield_suspender{_awaiter};
    }

    generator<T> get_return_object() {
        return generator<T>(this);
    }
    
    void return_void() {}
    
    bool done() {
        auto h = std::coroutine_handle<generator_promise<T> >::from_promise(*this);
        return h.done();
    }
    
    bool on_await_resume()  {
        _awaiter = nullptr;
        if (_e) std::rethrow_exception(_e);
        return !done();
    }

    bool next() {
        //avaiter must be null here
        assert(_awaiter == nullptr);
        //retrieve handle
        auto h = std::coroutine_handle<generator_promise<T> >::from_promise(*this);
        //if coroutine is already done - return false
        if (h.done()) return false;
        //resume and generate next item 
        //this call block until result is generated or until generator is suspended
        h.resume();
        //_awaiter is set to non-null when generator is suspended
        //in this case, result is not ready yet, we must perform synchronously waiting
        if (_awaiter) {
            //in this case syncing_awaiter is always there
            auto s = static_cast<syncing_awaiter *>(_awaiter);
            //wait for result
            s->wait();
            //delete awaiter
            delete s;
            //reset awaiter pointer
            _awaiter = nullptr;
        }
        //check for exception
        if (_e) std::rethrow_exception(_e);
        //return true, if value is non-null
        return _value != nullptr;
    }
    
    std::coroutine_handle<generator_promise<T> > next_async(awaiter *awt) {
        assert(_awaiter == nullptr);
        _awaiter = awt;
        return std::coroutine_handle<generator_promise<T> >::from_promise(*this);
    }

    template<typename X>
    X &&await_transform(X &&x) {
        //called when generator calls co_await. 
        //in this case - regardless on result - generator
        //can be suspended. If the awaiter is null, we performing sync
        //reading. So we create syncing_awaiter to able blocking wait
        
        //if awaiter is not null, awaiter can solve suspension by own.
        if (!_awaiter ) {
            _awaiter = new(_var_storage) syncing_awaiter();
        }
        //forward awaiter
        return std::forward<X>(x);
    }
    
    T *get() {return _value;}

    void after_callback_resume(awaiter *awt) {
        if (_awaiter == awt) _awaiter = nullptr;
    }
    
    template<typename Fn>
    void next_cb(Fn &&fn) {
        auto h = std::coroutine_handle<generator_promise<T> >::from_promise(*this);
        if (h.done()) {
            fn();
            return;
        }
        //when next_cb is called, there should be no pending awaiter        
        assert(_awaiter == nullptr);
        _awaiter = new (_var_storage) callback_awaiter<Fn>(*this, std::forward<Fn>(fn));
        h.resume();
    }
    
    
protected:
    T *_value = nullptr;
    awaiter *_awaiter = nullptr;
    std::exception_ptr _e;
    reusable_storage _var_storage;
    
    
};


///Not actual generator, just an interface
/**
 * @tparam T type of generator
 * 
 * stoppable generator can be stopped anytime from outside. Result of stopping could
 * be marking generator finished.
 * 
 * This is better solution than force destroying the generator, because you can't stop
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

#endif /* SRC_COCLASSES_GENERATOR2_H_ */
