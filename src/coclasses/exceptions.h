/**
 * @file exceptions.h
 */
#pragma once
#ifndef SRC_COCLASSES_EXCEPTIONS_H_
#define SRC_COCLASSES_EXCEPTIONS_H_

#include <stdexcept>


namespace cocls {

///Thrown from co_await when there is no promises which could resolve the future
class await_canceled_exception: public std::exception {
public:
    const char *what() const noexcept {
        return "Operation co_await has been canceled";
    }
};

///Thrown when asking value of the task which is not finished yet
class value_not_ready_exception: public std::exception {
public:
    const char *what() const noexcept {
        return "Can't retrieve value, it is not ready yet";
    }
};

class no_more_values_exception: public std::exception {
public:
    const char *what() const noexcept {
        return "No more values available or accepted";
    }
    
};
///Requested value is no longer available
class no_longer_avaible_exception: public std::exception {
public:
    const char *what() const noexcept {
        return "Requested value is no longer available";
    }
    
};


}




#endif /* SRC_COCLASSES_EXCEPTIONS_H_ */
