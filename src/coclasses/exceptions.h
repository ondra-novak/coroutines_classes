#pragma once
#ifndef SRC_COCLASSES_EXCEPTIONS_H_
#define SRC_COCLASSES_EXCEPTIONS_H_

#include <stdexcept>


namespace cocls {

class await_canceled_exception: public std::exception {
public:
    const char *what() const noexcept {
        return "Operation co_await has been canceled";
    }
};

class value_not_ready_exception: public std::exception {
public:
    const char *what() const noexcept {
        return "Can't retrieve value, it is not ready yet";
    }
};

}




#endif /* SRC_COCLASSES_EXCEPTIONS_H_ */
