#pragma once
#ifndef SRC_COCLASSES_COMMON_H_
#define SRC_COCLASSES_COMMON_H_


#ifdef __CDT_PARSER__
//This part is seen by Eclipse CDT Parser only
//eclipse doesn't support co_await and co_return, so let define some macros


//rewrite co_await to !, which correctly handles operator co_await -> operator! and co_await <expr> -> ! <expr> 
#define co_await !
//rewrite co_return as throw
#define co_return throw
#define co_yield throw
#define consteval constexpr
#endif




#endif /* SRC_COCLASSES_COMMON_H_ */
