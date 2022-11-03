/**
 * @file callback_generator.h
 */

#pragma once
#ifndef SRC_COCLASSES_CALLBACK_GENERATOR_H_
#define SRC_COCLASSES_CALLBACK_GENERATOR_H_
#include "exceptions.h"

#include <algorithm>
#include <coroutine>
#include <memory>
#include <optional>
#include <type_traits>

namespace cocls {

///Creates a callbacak generator
/**
 * Callback generator helps to adopt classic non-coroutine generator to be used 
 * with coroutines. It is expected, that such generator call a callback for every generated
 * value. The coroutine is put to a role of a collector. So the normal code runs generator
 * and a coroutine collects the data through co_await. 
 * 
 * The collecting coroutine must be synchronous. It can't co_await on anything else. It
 * is possible to co_yield generated data, so it can self act as generator, but the collection
 * side of such generator must be also coroutine because there cannot be more instances
 * of non-coroutine code at the same time. 
 * 
 * The callback generator can do recursion and generate values inside. This can be often
 * useful for recursive descending, for parsing and generating the code.   
 * 
 * The generator itself just calls a callback. The callback represents class collector<>, 
 * which is callable. By calling this object causes resumption of coroutine, which
 * can process the data. On next co_await, the coroutine exits, and callback returns to
 * the generator and the generator can continue.
 * 
 * @tparam Prototype defines callback prototype. Use callable type definition, similar to
 * std::function. For example bool(int) which declares, that callback accepts int and returns
 * bool. The coroutine then needs to call the instance of generator with a bool value and
 * the co_await returns int value. 
 * 
 * It is allowed to have void as return value, and it is allowed to have void as argument. 
 * However there is no support to have void(void) callback.
 * 

 * @param fn function to be executed as generator
 * @return instance of generator, the coroutine can co_await on it
 * 
 * @note the generator is not movable nor copyable. If you need such movable feature, use 
 * make_unique_callback_generator() 
 * 
 * @code
 *  //example, where callback returns true for new value and false for stop
 * auto gen = make_callback_generator<bool(int)>([&](auto &collector, bool run) {
 *      if (run) run_generator(collector); //if called with false, we don't even start the generator      
 * });
 * 
 * std::optional<int> v = co_await gen(true); //pass true as go-on, return is optional
 * while (v.has_value()) {
 *          std::cout<<*v<<std::endl;
 *          v = co_await gen(true); //we can pass false to stop generating 
 * }   
 * // when generator finishes, empty value is returned
 * @endcode
 * 
 * During the running generator, the coroutine can't be suspended for other reason,
 * Only exception is suspension with using a symmetric transfer, but the control
 * must be returned back to the coroutine otherwise, the generator receives the 
 * exception no_more_values_exception and generation is probably stopped (unless the 
 * generator handles this exception somehow). The same is applied when coroutine
 * exits before generation is finished (because exit on coroutine causes suspension
 * on final_suspend())
 * 
 */
template<typename Prototype, typename Fn>
inline auto make_callback_generator(Fn &&fn);

///Creates movable version of callback generator
/**
 * @copydoc make_callback_generator
 */
template<typename Prototype, typename Fn>
inline auto make_unique_callback_generator(Fn &&fn);

///Collector object
/**
 * A callable object which handles value passed to he call and transfers this
 * value to the coroutine. It can "suspend" running non-coroutine code temporarily
 * until the coroutine process the value and co_awaits again.
 * @tparam T Type of a value carried by the argument of the callback
 * @tparam R Type of a value returned from the callback
 */
template<typename T, typename R>
class collector;

///Helper class to act as backend for the collector
template<typename T, typename R>
class callback_generator_base {
public:
    using Arg = T;
    using Ret = R;

    callback_generator_base() = default;
    callback_generator_base(const callback_generator_base &gen) = delete;
    callback_generator_base &operator=(const callback_generator_base &gen) = delete;

    ///The collector call this function to register itself to be collected
    /**
     * @param coll pointer to collector
     * 
     * it is expected, that collector resumes associated coroutine
     */
    void collect(collector<T,R> *coll) {
        _cur_coll = coll;
    }
    
protected:
    collector<T,R> *_cur_coll = nullptr;
    
};


template<typename T, typename R>
class collector {
public:
    ///Inicialize collector
    /** This object is initialized by callback_generator's internals. You don't need to take care about it
     * 
     * @param gen reference to backend instance
     * @param h associated coroutine handle - it is needed for the first co_await, because
     * futher suspension are handled using set_resume_params
     */
    collector(callback_generator_base<T, R> &gen, std::coroutine_handle<> h):_gen(gen),_h(h) {}    
    collector(const collector &) = default;
    collector &operator=(collector &) = delete;
    
    ///A call, pass the value to the coroutine
    /**
     * Suspends current execution and executes associated coroutine, passess the value
     * as result of its co_await
     * @param val value as rvalue
     * @return value passed to co_await expression on next co_await
     * 
     * @note value is passed as std::move, the coroutine can modify it
     */
    R operator()(T &&val) {
        _val = &val;
        _gen.collect(this);        
        run_collector();
        return *_ret;
    }
    ///A call, pass the value to the coroutine
    /**
     * Suspends current execution and executes associated coroutine, passess the value
     * as result of its co_await
     * @param val value as lvalue
     * @return value passed to co_await expression on next co_await
     * 
     * @note coroutine can't modify the value despite on, it is passed as reference 
     */
    R operator()(T &val) {
        T tmp = val;
        _val = &tmp;
        _gen.collect(this);
        run_collector();
        return *_ret;
    }
    
    ///Sets resume parameters - for next resume, must be called from coroutine (before it is suspended)
    /**
     * @param h handle of coroutine
     * @param ret pointer to return value, which is eventually returned from the callback
     */
    void set_resume_params(std::coroutine_handle<> h, R *ret) {
        _h = h;
        _ret = ret;
    }
    
    ///Retrieve pointer to value passed as argument of the callback
    T *get_value() const {
        return _val;
    }
    
protected:
    T *_val = nullptr;
    R *_ret = nullptr;
    callback_generator_base<T, R> &_gen;
    std::coroutine_handle<> _h;

    void run_collector() {
        std::coroutine_handle<> h = _h;
        _h = nullptr;
        h.resume();
        if (!_h) {
            throw no_more_values_exception();
        }
    }
};


///Collector object
/**
 * A callable object which handles value passed to he call and transfers this
 * value to the coroutine. It can "suspend" running non-coroutine code temporarily
 * until the coroutine process the value and co_awaits again.
 * 
 * This object doesn't expects return value of the callback
 * 
 * @tparam T The type of a value passed to the callback as argument
 * 
 */

template<typename T>
class collector<T,void> {
public:
    collector(callback_generator_base<T, void> &gen, std::coroutine_handle<> h):_gen(gen),_h(h) {}
    collector(const collector &) = default;
    collector &operator=(collector &) = delete;
    
    ///A call, pass the value to the coroutine
    /**
     * Suspends current execution and executes associated coroutine, passess the value
     * as result of its co_await
     * @param val value as rvalue
     * 
     * @note value is passed as std::move, the coroutine can modify it
     */
    void operator()(T &&val) {
        _val = &val;
        _gen.collect(this);        
        run_collector();
    }
    ///A call, pass the value to the coroutine
    /**
     * Suspends current execution and executes associated coroutine, passess the value
     * as result of its co_await
     * @param val value as lvalue
     * 
     * @note coroutine can't modify the value despite on, it is passed as reference 
     */
    void operator()(T &val) {
        T tmp = val;
        _val = &tmp;
        _gen.collect(this);
        run_collector();
    }

    
    ///Sets resume parameters - for next resume, must be called from coroutine (before it is suspended)
    /**
     * @param h handle of coroutine
     */
    void set_resume_params(std::coroutine_handle<> h) {
        _h = h;
    }
    
    ///Retrieve pointer to value passed as argument of the callback
    T *get_value() const {
        return _val;
    }
    
protected:
    T *_val = nullptr;
    callback_generator_base<T, void> &_gen;
    std::coroutine_handle<> _h;

    void run_collector() {
        std::coroutine_handle<> h = _h;
        _h = nullptr;
        h.resume();
        if (!_h) {
            throw no_more_values_exception();
        }
    }
    
};

///Collector object
/**
 * A callable object which handles value passed to he call and transfers this
 * value to the coroutine. It can "suspend" running non-coroutine code temporarily
 * until the coroutine process the value and co_awaits again.
 * 
 * This object doesn't expects arguments for the callback, just return value
 * 
 * @tparam R Type of a value returned from the callback
 * 
 */
template<typename R>
class collector<void, R> {
public:
    collector(callback_generator_base<void, R> &gen, std::coroutine_handle<> h):_gen(gen),_h(h) {}
    collector(const collector &) = default;
    collector &operator=(collector &) = delete;
    
    ///A call, pass the value to the coroutine
    /**
     * Suspends current execution and executes associated coroutine
     * @return value passed to co_await expression on next co_await
     */
    R operator()() {
        _gen.collect(this);        
        run_collector();
        return *_ret;
    }
    
    ///Sets resume parameters - for next resume, must be called from coroutine (before it is suspended)
    /**
     * @param h handle of coroutine
     * @param ret pointer to return value, which is eventually returned from the callback
     */
    void set_resume_params(std::coroutine_handle<> h, R *ret) {
        _h = h;
        _ret = ret;
    }
    
    
protected:
    R *_ret = nullptr;
    callback_generator_base<void, R> &_gen;
    std::coroutine_handle<> _h;

    void run_collector() {
        std::coroutine_handle<> h = _h;
        _h = nullptr;
        h.resume();
        if (!_h) {
            throw no_more_values_exception();
        }
    }
    
};


///Callback generator
/**
 * To construct this class, you can use convenient way by calling the function
 * make_callback_generator(). However a construction as standard object
 * can be useful when the object is part of other declarations, for example
 * a member of other class. In this case, you need to determine type of the
 * function which appears as one of template arguments
 * 
 * @tparam T type of argument passed to the callback = can be void
 * @tparam R return value from the callback = can be void
 * @tparam Fn the function type
 */
template<typename T, typename R, typename Fn>
class callback_generator: public callback_generator_base<T,R> {
public:
    using super = callback_generator_base<T,R>;

    ///construct the generator, pass function as rvalue
    callback_generator(Fn &&fn):_fn(std::forward<Fn>(fn)) {}

    ///Awaiter
    class awaiter {
    public:
        awaiter(callback_generator &owner):_owner(owner) {}
        awaiter(const awaiter &owner)= default;
        awaiter &operator=(const awaiter &owner)= delete;
        
        static bool await_ready() {            
            return false;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            if (_owner.handle_suspend(h)) return std::noop_coroutine();
            else return h;
        }
        decltype(auto) await_resume() {
            if constexpr(!std::is_void<T>::value) {
                if (!_owner._cur_coll) {
                    return std::optional<T>();
                } else {
                    return std::optional<T>(std::move(*_owner._cur_coll->get_value()));
                }
            } else {
                return _owner._cur_coll != nullptr;                
            }
        }
        
        
    protected:
        callback_generator &_owner;
        std::exception_ptr _exp;
        
    };
    
    ///Resume generation
    /**
     * Result of this expression is awaiter, so you need to co_await on
     * it. Note that awaiter @b doesn't support resumption_policy, because the
     * coroutine is always resumed on top of the generator
     * 
     * @param r value passed to the generator which appears as return value of the callback
     * @return awaiter object. On successful await, result is std::optional<T>. 
     * It contains next generated value. If the optional is empty, generator just finished
     * 
     * @code
     * auto gen = make_callback_generator<bool(int)>(...);
     * int val = co_await gen(true);  
     * @endcode
     * 
     * 
     */
    awaiter operator()(R &&r) {
        _ret = &r;
        return awaiter(*this);
    }
    ///Resume generation
    /**
     * Result of this expression is awaiter, so you need to co_await on
     * it. Note that awaiter @b doesn't support resumption_policy, because the
     * coroutine is always resumed on top of the generator
     * 
     * @param r value passed to the generator which appears as return value of the callback
     * @return awaiter object. On successful await, result is std::optional<T>. 
     * It contains next generated value. If the optional is empty, generator just finished
     * 
     * @code
     * auto gen = make_callback_generator<bool(int)>(...);
     * int val = co_await gen(true);  
     * @endcode
     */
    awaiter operator()(R &r) {
        _ret = &r;
        return awaiter(*this);
    }

    
    
protected:
    R *_ret = nullptr;
    Fn _fn;
    bool _started = false;
    
    bool handle_suspend(std::coroutine_handle<> h) {
        if (_started) {
            if (this->_cur_coll) {
                this->_cur_coll->set_resume_params(h, _ret);
                this->_cur_coll = nullptr;
                return true;
            } 
            throw no_more_values_exception();            
        } else {
            collector<T,R> col(*this, h);
            _started = true;
            _fn(col,*this->_ret);
            return false;
        }
    }
    
};

///Callback generator
/**
 * To construct this class, you can use convenient way by calling the function
 * make_callback_generator(). However a construction as standard object
 * can be useful when the object is part of other declarations, for example
 * a member of other class. In this case, you need to determine type of the
 * function which appears as one of template arguments
 *
 * This is specialization for generator with void return
 * 
 * @tparam T type of argument passed to the callback = can be void
 * @tparam Fn the function type
 */
template<typename T, typename Fn>
class callback_generator<T,void,Fn>: public callback_generator_base<T,void> {
public:
    using super = callback_generator_base<T,void>;

    ///construct the generator, pass function as rvalue
    callback_generator(Fn &&fn):_fn(std::forward<Fn>(fn)) {}

    
    class awaiter {
    public:
        awaiter(callback_generator &owner):_owner(owner) {}
        awaiter(const awaiter &owner)= default;
        awaiter &operator=(const awaiter &owner)= delete;
        
        static bool await_ready() {            
            return false;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            if (_owner.handle_suspend(h)) return std::noop_coroutine();
            else return h;
        }
        decltype(auto) await_resume() {
            if constexpr(!std::is_void<T>::value) {
                if (!_owner._cur_coll) {
                    return std::optional<T>();
                } else {
                    return std::optional<T>(std::move(*_owner._cur_coll->get_value()));
                }
            } else  {
                return _owner._cur_coll != nullptr;                
            } 
        }
        
        
    protected:
        callback_generator &_owner;
        std::exception_ptr _exp;
        
    };
    
    ///Resume generation
    /**
     * Result of this expression is awaiter, so you need to co_await on
     * it. Note that awaiter @b doesn't support resumption_policy, because the
     * coroutine is always resumed on top of the generator
     * 
     * @return awaiter object. On successful await, result is std::optional<T>. 
     * It contains next generated value. If the optional is empty, generator just finished
     * 
     * @code
     * auto gen = make_callback_generator<void(int)>(...);
     * int val = co_await gen();  
     * @endcode
     */
    awaiter operator()() {
        return awaiter(*this);
    }

    
protected:
    Fn _fn;
    bool _started = false;
    
    bool handle_suspend(std::coroutine_handle<> h) {
        if (_started) {
            if (this->_cur_coll) {
                this->_cur_coll->set_resume_params(h);
                this->_cur_coll = nullptr;
                return true;
            } 
            throw no_more_values_exception();            
        } else {
            collector<T,void> col(*this, h);
            _started = true;
            _fn(col);
            return false;
        }
    }
    
};


namespace _details {


template<typename Prototype> class CrackPrototype;
template<typename R, typename T> class CrackPrototype<R(T)> {
public:
    using Arg = T;
    using Ret = R;
};


}
template<typename Prototype, typename Fn>
inline auto make_callback_generator(Fn &&fn) {
    using Arg = typename _details::CrackPrototype<Prototype>::Arg;
    using Ret = typename _details::CrackPrototype<Prototype>::Ret;
    return callback_generator<Arg, Ret, Fn>(std::forward<Fn>(fn));
}

template<typename Prototype, typename Fn>
inline auto make_unique_callback_generator(Fn &&fn) {
    using Arg = typename _details::CrackPrototype<Prototype>::Arg;
    using Ret = typename _details::CrackPrototype<Prototype>::Ret;
    return std::make_unique<callback_generator<Arg, Ret, Fn> >(std::forward<Fn>(fn));
    
}


}




#endif /* SRC_COCLASSES_CALLBACK_GENERATOR_H_ */
