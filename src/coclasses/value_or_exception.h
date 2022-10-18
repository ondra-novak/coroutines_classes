#pragma once
#ifndef SRC_COCLASSES_VALUE_OR_EXCEPTION_H_
#define SRC_COCLASSES_VALUE_OR_EXCEPTION_H_
#include "exceptions.h"

#include <exception>
#include <variant>

namespace cocls {

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


}



#endif /* SRC_COCLASSES_VALUE_OR_EXCEPTION_H_ */
