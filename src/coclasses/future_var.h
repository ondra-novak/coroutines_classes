#pragma once
#ifndef SRC_COCLASSES_FUTURE_VAR_H_
#define SRC_COCLASSES_FUTURE_VAR_H_
#include "exceptions.h"

#include <exception>
#include <variant>

namespace cocls {


///future variable - just a storage of value, similar to std::optional
/** you can create it uninitialized and initialize it later.
 * You can also store an exception instead if the value cannot be calculated
 * 
 * @tparam T type of value
 */
template<typename T>
class future_var {
public:
    ///State of future variable
    enum State: char {
        ///value is not set
        not_set,
        ///value is set
        value,
        ///exception is set
        exception
    };

    
    ///Construct uninitialized
    future_var():_st(not_set) {}
    ///Construct initialized with a value
    future_var(const T &x):_st(value),_v(x) {}
    ///Construct initialized with a value
    future_var(T &&x):_st(value),_v(std::move(x)) {}

    ///destruct
    ~future_var() {
        reset();
    }
    ///copy constructor
    future_var(const future_var &other):_st(other._st) {
        switch(_st) {
            case value: new(&_v) T(other._v); break;
            case exception: new(&_e) std::exception_ptr(other._e); break;
            default:break;
        }
    }
    ///move constructor
    /**
     * @param other other variable
     * @note other variable remains uninitialized
     */
    future_var(future_var &&other):_st(other._st) {
        switch(_st) {
            case value: new(&_v) T(std::move(other._v)); break;
            case exception: new(&_e) std::exception_ptr(std::move(other._e)); break;
            default:break;
        }        
        other.reset();
    }
    ///assignment
    future_var &operator=(const future_var &other) {
        if (this != &other) {
            this->~future_var();
            new(this) future_var(other);
        }
        return *this;            
    }
    ///move assignment
    /**
     * @param other other variable
     * @note other variable remains uninitialized
     */
    future_var &operator=(future_var &&other) {
        if (this != &other) {
            this->~future_var();
            new(this) future_var(std::move(other));
        }
        return *this;            
    }

    ///emplace new value (construct inside the object)
    /**
     * @param args arguments passed to the constructor
     */
    template<typename ... Args>
    void emplace(Args && ... args) {
        reset();
        new(&_v) T(std::forward<Args>(args)...);
        _st = value;
    }

    template<typename Fn, typename ... Args>
    void transform(Fn &&fn, Args && ... args) {
        emplace(fn(std::forward<Args>(args)...));
    }
    
    ///set by moving
    /**
     * @param v value to be moved in
     */
    void set(T && v) {
        reset();
        new(&_v) T(std::move(v));
        _st = value;
    }

    ///set by copying
    /**
     * @param v value to by copied
     */
    void set(const T &v) {
        reset();
        new(&_v) T(v);
        _st = value;
    }

    ///reset content
    void reset() {
        switch(_st) {
            case value: _v.~T();break;
            case exception: _e.~exception_ptr();break;
            default: return;
        }
        _st = not_set;
    }
    ///call this function inside catch handler
    /**
     * This stores current exception to the value, which allows to propagate
     * exception.
     */
    void unhandled_exception() {
        reset();
        auto e = std::current_exception();
        if (e) {
            new(&_e) std::exception_ptr(std::move(e));
            _st = exception;
        }
    }
    ///Get value
    /**
     * @return stored value
     * @note if the exception is stored, it is rethrown now
     */
    T &get() {
        switch(_st) {
            case value: return _v;
            case exception: std::rethrow_exception(_e);throw;
            default: throw value_not_ready_exception();
        }
    }
    ///Get value
    /**
     * @return stored value
     * @note if the exception is stored, it is rethrown now
     */
    const T &get() const {
        switch(_st) {
            case value: return _v;
            case exception: std::rethrow_exception(_e);throw;
            default: throw value_not_ready_exception();
        }
    }
    ///Determines whether value is set
    /**
     * @retval true value is set (however, it can be exception)
     * @retval false value is not set yet
     */
    bool has_value() const {
        return _st != not_set;
    }
    
    ///Retrieve current state
    State state() const {
        return _st;
    }
    ///Retrieve exception state
    /**
     * @return returns stored exception, or nullptr if no exception is stored
     */
    std::exception_ptr exception_ptr() const {
        return _st == exception?_e:nullptr; 
    }
    
protected:
    union {
        T _v;
        std::exception_ptr _e;        
    };
    State _st;    
};

template<>
class future_var<void> {
public:
    enum State {
        not_set,
        value,
        exception
    };
    
    future_var():_st(not_set) {}
    future_var(std::nullptr_t):_st(value) {}
    
    void emplace() {
        _st = value;        
    }

    template<typename Fn, typename ... Args>
    void transform(Fn &&fn, Args && ... args) {
        fn(std::forward<Args>(args)...);
        emplace();
    }

    void reset() {
        _st = not_set;
    }
    
    void get() const {
        switch(_st) {
            case value: return;
            case exception: std::rethrow_exception(_e);throw;
            default: throw value_not_ready_exception();
        }
    }
    bool has_value() const {
        return _st != not_set;
    }
    
    State state() const {
        return _st;
    }
    std::exception_ptr exception_ptr() const {
        return _st == exception?_e:nullptr; 
    }

    void unhandled_exception() {
        _e = std::current_exception();
        _st = _e?exception:not_set;        
    }

protected:
    
    State _st;
    std::exception_ptr _e;
};



}



#endif /* SRC_COCLASSES_FUTURE_VAR_H_ */
