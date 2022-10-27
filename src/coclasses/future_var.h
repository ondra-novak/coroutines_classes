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
    enum State: char {
        not_set,
        value,
        exception
    };

    
    future_var():_st(not_set) {}
    future_var(const T &x):_st(value),_v(x) {}    
    future_var(T &&x):_st(value),_v(std::move(x)) {}

    ~future_var() {
        reset();
    }
    future_var(const future_var &other):_st(other._st) {
        switch(_st) {
            case value: new(&_v) T(other._v); break;
            case exception: new(&_e) std::exception_ptr(other._e); break;
            default:break;
        }
    }
    future_var(future_var &&other):_st(other._st) {
        switch(_st) {
            case value: new(&_v) T(std::move(other._v)); break;
            case exception: new(&_e) std::exception_ptr(std::move(other._e)); break;
            default:break;
        }        
        other.reset();
    }
    future_var &operator=(const future_var &other) {
        if (this != &other) {
            this->~future_var();
            new(this) future_var(other);
        }
        return *this;            
    }
    future_var &operator=(future_var &&other) {
        if (this != &other) {
            this->~future_var();
            new(this) future_var(std::move(other));
        }
        return *this;            
    }

    template<typename ... Args>
    void emplace(Args && ... args) {
        reset();
        new(&_v) T(std::forward<Args>(args)...);
        _st = value;
    }

    void emplace(T && v) {
        reset();
        new(&_v) T(std::move(v));
        _st = value;
    }

    void emplace(const T &v) {
        reset();
        new(&_v) T(v);
        _st = value;
    }

    void reset() {
        switch(_st) {
            case value: _v.~T();break;
            case exception: _e.~exception_ptr();break;
            default: return;
        }
        _st = not_set;
    }
    void unhandled_exception() {
        reset();
        auto e = std::current_exception();
        if (e) {
            new(&_e) std::exception_ptr(std::move(e));
            _st = exception;
        }
    }
    T &get() {
        switch(_st) {
            case value: return _v;
            case exception: std::rethrow_exception(_e);throw;
            default: throw value_not_ready_exception();
        }
    }
    const T &get() const {
        switch(_st) {
            case value: return _v;
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


#if 0
///Holds value or exception
/**
 * Need to store coroutine result
 * 
 * @tparam T
 */
template<typename T>
class value_or_exception {
public:
    
    ///Set value
    /**    
     * @param val value to set
     */
    template<typename X>
    void set_value(X &&val) noexcept {
        try {
            _value.template emplace<T>(std::forward<X>(val));
        } catch (...) {
            unhandled_exception();
        }
    }
    
    ///Capture unhandled exception
    void unhandled_exception() {
        _value.template emplace<std::exception_ptr>(std::current_exception());
    }
    
    ///Get value as reference (can be moved out)
    /**
     * @return stored value. Function can throw an exception, if exception is stored
     */
    T &get_value() {
        if (_value.index() ==0) {
            std::exception_ptr p = std::get<0>(_value);
            if (p == nullptr) throw value_not_ready_exception();
            else std::rethrow_exception(p);
        } else {
            return get_value_int();
        }
    }
    

    ///Get value as const reference
    /**
     * @return stored value. Function can throw an exception, if exception is stored
     */
    const T &get_value() const {
        if (_value.index() ==0) {
            std::exception_ptr p = std::get<0>(_value);
            if (p == nullptr) throw value_not_ready_exception();
            else std::rethrow_exception(p);
        } else {
            return get_value_int();
        }
    }

    ///Returns true, if there is a value or exception
    /**
     * @retval true result is ready
     * @retval false result is not ready yet 
     */
    bool is_ready() {
        return _value.index() == 1 || std::get<0>(_value) != nullptr;
    }
    ///Retrieve exception if is there
    /**
     * @return returns exception or nullptr
     */
    std::exception_ptr get_exception() const noexcept {
        if (_value.index() != 0) return nullptr;
        else return std::get<0>(_value); 
    }
protected:
    
    T &get_value_int() noexcept {
        return std::get<1>(_value);
    }
    const T &get_value_int() const noexcept {
        return std::get<1>(_value);
    }
    
    std::variant<std::exception_ptr, T> _value;

};

template<>
struct value_or_exception<void>: public value_or_exception<bool> {

    void set_value() {
        value_or_exception<bool>::set_value(true);
    }
    
    void get_value() {
        value_or_exception<bool>::get_value();
    }
    
};
#endif

}



#endif /* SRC_COCLASSES_FUTURE_VAR_H_ */
