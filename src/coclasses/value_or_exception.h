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
struct value_or_exception {
    
    template<typename X>
    void set_value(X &&val) {
        _value.template emplace<T>(std::forward<X>(val));
    }
    
    void unhandled_exception() {
        _value.template emplace<std::exception_ptr>(std::current_exception());
    }
        
    std::variant<std::exception_ptr, T> _value;
    
    T &get_value() {
        if (_value.index() ==0) {
            std::exception_ptr p = std::get<0>(_value);
            if (p == nullptr) throw value_not_ready_exception();
            else std::rethrow_exception(p);
        } else {
            return std::get<1>(_value);
        }
    }
    
    bool is_ready() {
        return _value.index() == 1 || std::get<0>(_value) != nullptr;
    }
    
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
