/*
MIT License

Copyright (c) 2022 Ondrej Novak

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <atomic>
#include <coroutine>
#include <stdexcept>
#include <future>
#include <optional>

namespace cocls {

class callback_destroyed_exception: public std::exception {
public:
    const char *what() const noexcept {
        return "The callback was destroyed before being called";
    }
};

class value_not_ready_exception: public std::exception {
public:
    const char *what() const noexcept {
        return "Can't retrieve value, it is not ready yet";
    }
};


///Task object, it is returned from the coroutine
/**
 * @code
 * task<int> cofunction() {
 *      co_return 42;
 * }
 * @endcode
 * 
 * Task is actually a kind of a smart pointer which holds the
 * coroutine frame even if the coroutine is already finished. You 
 * can copy this object, or await this object. You can multiple await
 * at the time. Once the result is available, all waiting coroutines
 * are resumed
 */
template<typename T = void> class task;

///Coroutine promise object - part of coroutine frame
/** This object is never used directly, but it is essential object to support coroutines
 * 
 */
template<typename T> class task_promise;

///Awaiter class
/** Awaiters are helper objects created for co_await. They
 * handles resumption of a suspended coroutine
 */
template<typename Imp> class task_awaiter;

///Class which helps to make promise which is fulfilled through a callback
/**
 * @code
 * task<int> cofunction() {
 *      callback_promise<int> cbpromise;
 *      do_async_work(cbpromise.get_callback());
 *      int i = co_await cbpromise;
 *      co_return i; 
 * }
 * @endcode
 */
template<typename T> class callback_promise;



template<typename T> class task {
public:
    using promise_type = task_promise<T>;

    ///You can create empty task, which can be initialized later by assign
    task():_promise(nullptr) {}
    ///task is internaly constructed from pointer to a promise  
    task(task_promise<T> *promise): _promise(promise) {
        _promise->add_ref();
    }
    ///destruction of task decreases reference
    ~task() {
        if (_promise) _promise->release_ref();
    }
    
    ///you can copy task, which just increases references
    task(const task &other):_promise(other._promise) {
        _promise->add_ref();
    }
    ///you can move task
    task(task &&other):_promise(other._promise) {
        other._promise = nullptr;
    }
    ///you can assign
    task &operator=(const task &other){
        if (this != &other) {
            if (_promise) _promise->release_ref();
            _promise = other._promise;
            _promise->add_ref();
        }
        return *this;
    }
    ///you can move-assign
    task &operator=(task &&other) {
        if (this != &other) {
            if (_promise) _promise->release_ref();
            _promise = other._promise;
            other._promise = nullptr;        
        }
        return *this;
    }
    ///task can be awaited
    /**
     * @return awaiter
     * 
     * awaiting the task causes suspend of curreny coroutine until
     * the awaited task is finished
     */
    task_awaiter<task_promise<T> > operator co_await() {
        return task_awaiter<task_promise<T> >(*_promise);
    }
    
    ///Helps to connect normal-function world with coroutine world, creates waitable future
    /**
     * @return waitable future. The future is fulfilled when task is done.
     * Note when you attempt to wait on this future, remember that function
     * is blocking. If the task cannot be finished because of this blocking
     * call then deadlock is result of a such execution.
     */
    std::future<T> get_future() {
        std::future<T> out;
        future_wait(*this, out);
        return out;
    }
    
    ///Helps to connect normal-function world witj coroutine world, calls a callback when task is done
    /**
     * @param fn callback function. The callback function is called when task is done and receives result of the task
     */
    template<typename Fn>
    void on_finish(Fn &&fn) {
        callback_wait(this, std::forward<Fn>(fn));
    }
    
protected:
    task_promise<T> *_promise;
    
    ///helper fun
    static task<void> future_wait(task<T> t, std::future<T> &f) {        
        std::promise<T> prom;
        f = prom.get_future();
        prom.set_value(co_await (t));        
    }
    ///helper fun
    template<typename Fn>
    static task<void> callback_wait(task<T> t, Fn &&fn) {
        //function is copied, because can become unavailable during suspension
        Fn cb (std::forward<Fn>(fn));
        cb(co_await(t));
    }
};


template<typename Owner> class  task_awaiter {
public:
    task_awaiter(Owner &owner): _owner(owner) {}
    
    bool await_ready() const noexcept {
        return _owner.is_ready();
    }
    bool await_suspend(std::coroutine_handle<> h) {
        _h = h;
        return _owner.register_awaiter(this);
    }
    auto &await_resume() {
        return _owner.get_value();
    }
    void resume() {
        _h.resume();
    }

    void push_to(std::atomic<task_awaiter<Owner> *> &list) {
        while (!list.compare_exchange_weak(next, this));
    }
    
    task_awaiter *get_next() {
        return next;
    }
protected:
    task_awaiter *next = nullptr;
    Owner &_owner;
    std::coroutine_handle<> _h;
    

};

template<typename Impl> class task_promise_base {
public:
    
    task_promise_base():_ready(false),_awaiters(nullptr) {}

    void resolve(task_awaiter<Impl> *dont_resume) {
        task_awaiter<Impl> *list = _awaiters.exchange(nullptr);
        while (list) {
            auto *p = list;
            list = list->get_next();
            if (p != dont_resume) p->resume();            
        }
    }
    
    bool is_ready() const {
        return _ready;
    }

    bool register_awaiter(task_awaiter<Impl> *ptr) {
        if (_ready) return false;
        ptr->push_to(_awaiters);
        if (_ready) {
            resolve(ptr);
            return false;
        }
        return true;
    }
    
    auto get_value() {
        return static_cast<const Impl &>(*this).get_value();
    }
    
protected:
    std::atomic<bool> _ready;
    std::atomic<task_awaiter<Impl> *> _awaiters;

    
};

template<typename Impl> class task_coroutine_promise {
public:
    task_coroutine_promise():_ref_count(1) {}

    task_coroutine_promise(const task_coroutine_promise &) = delete;
    task_coroutine_promise &operator=(const task_coroutine_promise &) = delete;

    class final_awaiter {
    public:
        final_awaiter(task_coroutine_promise &prom): _owner(prom) {}
        bool await_ready() const noexcept {
            return _owner._ref_count == 0;
        }
        constexpr void await_suspend(std::coroutine_handle<>) const noexcept {}
        constexpr void await_resume() const noexcept {}        
    protected:
        task_coroutine_promise &_owner;
    };
    
    std::suspend_never initial_suspend() const noexcept {return {};}
    final_awaiter final_suspend() noexcept {
        --_ref_count;
        return *this;
    }
    
    void add_ref() {
        _ref_count++;
    }
    void release_ref() {
        if (--_ref_count == 0) {
            Impl &me = static_cast<Impl &>(*this);
            auto h = std::coroutine_handle<Impl >::from_promise(me);
            h.destroy();
        }
    }

    
protected:
    std::atomic<unsigned int> _ref_count;
};

template<typename T> class task_promise:
        public task_coroutine_promise<task_promise<T> >,
        public task_promise_base<task_promise<T> >
        
{
public:
    
    using awaiter = task_awaiter<task_promise<T> >;
    
    task_promise(): _is_exception(false)  {}
    ~task_promise() {
        if (this->is_ready()) {
            if (_is_exception) {
                _exception.~exception_ptr();
            } else {
                _value.~T();
            }
        }
    }

    typename task_coroutine_promise<task_promise<T> >::final_awaiter final_suspend() noexcept {
        this->resolve(nullptr);
        return task_coroutine_promise<task_promise<T> >::final_suspend();
    }

    
    void return_value(T &&val) {
        if (!this->_ready) {
            new(&_value) T(std::move(val));
            this->_ready = true;
        }
    }

    void return_value(const T &val) {
        if (!this->_ready) {
            new(&_value) T(val);
            this->_ready = true;
        }
    }

    void unhandled_exception() {
        set_exception(std::current_exception());
    }
    
    void set_exception(std::exception_ptr &&e) {
        if (!this->_ready) {
            new(&_exception) std::exception_ptr(e);
            _is_exception = true;
            this->_ready = true;
        }        
    }
    
    T &get_value()  {
        if (!this->_ready) {
            throw value_not_ready_exception();
        }
        if (_is_exception) {
            std::rethrow_exception(_exception);
        } else {
            return _value;
        }
    }
        
    
    
    task<T> get_return_object() {
        return task<T>(this);
    }
    
    
protected:
    bool _is_exception;
        
    union {
        T _value;
        std::exception_ptr _exception;
    };
};

template<> class task_promise<void>: 
        public task_promise_base<task_promise<void> >,
        public task_coroutine_promise<task_promise<void> >
{
public:
    
    typename task_coroutine_promise<task_promise<void> >::final_awaiter final_suspend() noexcept {
        this->resolve(nullptr);
        return task_coroutine_promise<task_promise<void> >::final_suspend();
    }

    
    void unhandled_exception() {
        set_exception(std::current_exception());
    }
    
    void set_exception(std::exception_ptr &&e) {
        if (!this->_ready) {
            _exception = std::move(e);
        }        
    }
    
    void get_value()  {
        if (!this->_ready) {
            throw value_not_ready_exception();
        }
        if (_exception) {
            std::rethrow_exception(_exception);
        }
    }
        
    
    void return_void() {
        this->_ready = true;
    }
    
    task<void> get_return_object() {
        return task<void>(this);
    }
    
    
protected:
    std::exception_ptr _exception;
};


template<typename T> 
class callback_promise: protected task_promise_base<callback_promise<T> > {
public:
    
    ///Calback promise is constructed as standard variable
    /** it cannot be moved or copied */
    callback_promise():_cbout(0) {}
    
    class callback {
    public:
        callback(callback_promise &owner):_owner(owner) {
            _owner.add_cb();
        }
        callback(const callback &other):_owner(other._owner) {
            _owner.add_cb();
        }
        ~callback() {
            _owner.release_cb();
        }
        
        void operator()(T &&val) const {
            _owner.set_value(std::move(val));
        }
        void operator()(const T &val) const {
            _owner.set_value(val);
        }
        
    protected:        
        callback_promise &_owner;
    };
    
    ///Gets current value (better - use co_await)
    /**
     * @return retrieves value only if the promise is fulfilled. 
     */
    T &get_value() {
       if (_value.has_value()) {
           return *_value;
       } 
       if (_exception) {
           std::rethrow_exception(_exception);
       }
       throw value_not_ready_exception();
       
    }
    
    ///Creates callback function
    /**     
     * @return returns function which can be called to resume awaiting
     * coroutine. Callback object can be copied, shared, but called only once!
     *
     * @note if the callback function is destroyed and there is no more
     * callback functions pending, the promise is resolved with the 
     * exception callback_destroyed_exception()
     * 
     */
    callback get_callback() {
        return callback(*this);
    }
    
    /// Retrieves captured exception
    /**
     * If there were an pending exception during call of the callback,
     * it is captured as well. You can then retrieve such exception. 
     * The purpose of this function is this: The callback can be called
     * with a special value, which means "error, pending exception contains
     * detail". This exception is captured and available now. 
     * @return captured exception
     */
    const std::exception_ptr &get_exception() {
        return _exception;
    }
    /// Retrieves captured exception
    /**
     * If there were an pending exception during call of the callback,
     * it is captured as well. You can then retrieve such exception. 
     * The purpose of this function is this: The callback can be called
     * with a special value, which means "error, pending exception contains
     * detail". This exception throw captured exception now.  
     * 
     * Note if there is no exception, function returns immediately
     */
    void throw_exception() {
        if (_exception != nullptr) std::rethrow_exception(_exception);
    }
    
    ///handles co_await
    task_awaiter<callback_promise<T> > operator co_await() {
        return task_awaiter<callback_promise<T> >(*this);
    }
    
    ///Sets value of the promise directly
    void set_value(T &&v) {
        _exception = std::current_exception();
        _value = std::move(v);
        this->_ready = true;
    }
    ///Sets value of the promise directly
    void set_value(const T &v) {
        _exception = std::current_exception();
        _value = v;
        this->_ready = true;
    }
protected:    
    
    friend class task_awaiter<callback_promise<T> >;
    std::atomic<unsigned int> _cbout;
    std::exception_ptr _exception;
    std::optional<T> _value;
    
    void add_cb() {
        ++_cbout;
    }
    void release_cb() {
        if (--_cbout == 0) {
            if (this->_ready) {
                this->resolve(nullptr);
            } else {
                _exception = std::make_exception_ptr(callback_destroyed_exception());
                this->resolve(nullptr);
            }
        }
    }
};

}
