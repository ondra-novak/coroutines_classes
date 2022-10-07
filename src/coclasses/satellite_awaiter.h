#pragma once
#ifndef SRC_COCLASSES_SRC_COCLASSES_SATELLITE_AWAITER_H_
#define SRC_COCLASSES_SRC_COCLASSES_SATELLITE_AWAITER_H_


namespace cocls {


///Helps to create awaiters created as satellite to primary object
template<typename Owner>
class satellite_awaiter {
public:
    using super_t = satellite_awaiter<Owner>;
    
    satellite_awaiter(Owner &owner):_owner(owner) {}
    satellite_awaiter(const satellite_awaiter &) = default;
    satellite_awaiter &operator=(const satellite_awaiter &) = delete;
protected:
    Owner &_owner;
};

}



#endif /* SRC_COCLASSES_SRC_COCLASSES_SATELLITE_AWAITER_H_ */
