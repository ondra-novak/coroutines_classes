/*
 * coid.h
 *
 *  Created on: 6. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_COROID_H_
#define SRC_COCLASSES_COROID_H_


#include <coroutine>

namespace cocls {

///Coroutine identifier
/**
 * Main purpose of coroutine identifier to allow identify coroutine, which means
 * finding coroutine in a list, and so on, but to prevent to give a user
 * coroutine handle directly, because it could be dangerous object. Because
 * coroutine handle allows to the holder to resume or destroy coroutine which
 * can break certain relations required by this library.
 * 
 * coroid_t can be just compared and cannot be used to control coroutines
 * 
 */
class coroid_t {
public:
    coroid_t() = default;    
    coroid_t(std::coroutine_handle<> h):_h(h.address()) {}
    
    bool operator == (const coroid_t &other) const {return _h == other._h;}
    bool operator != (const coroid_t &other) const {return _h != other._h;}
    bool operator >= (const coroid_t &other) const {return _h >= other._h;}
    bool operator <= (const coroid_t &other) const {return _h <= other._h;}
    bool operator > (const coroid_t &other) const {return _h > other._h;}
    bool operator < (const coroid_t &other) const {return _h < other._h;}
    
    
    
private:
    const void *_h = nullptr;
};

}





#endif /* SRC_COCLASSES_COROID_H_ */
