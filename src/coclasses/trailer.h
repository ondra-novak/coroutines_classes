/*
 * trail.h
 *
 *  Created on: 21. 2. 2023
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_TRAILER_H_
#define SRC_COCLASSES_TRAILER_H_
#include <algorithm>


namespace cocls {

///Trailer is object, which executes function at the end of block
/**
 * Trailer is called even after return.
 * @tparam Fn function to call
 */
template<typename Fn>
class trailer {
public:
    trailer(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
    ~trailer() noexcept(false) {
        _fn();
    }
protected:
    Fn _fn;
};

template<typename Fn>
trailer(Fn) -> trailer<Fn>;

}




#endif /* SRC_COCLASSES_TRAIL_H_ */
