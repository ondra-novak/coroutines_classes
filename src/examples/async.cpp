/**
 * @file async.cpp demonstration of async
 * 
 */


#include <coclasses/async.h>
#include <coclasses/scheduler.h>
#include <coclasses/dispatcher.h>


#include <iostream>
cocls::async<void> async_3() {
    co_await cocls::dispatcher::sleep_for(std::chrono::seconds(1));
    
}

cocls::async<int> async_2() {
    co_await cocls::dispatcher::sleep_for(std::chrono::seconds(1));
    co_return 42;
    
}


cocls::async<int> async_1() {
    co_await async_3();
    auto as = async_2();
    auto as2 = std::move(as);
    int res = co_await as2;
    co_return res;
}



int main(int, char **) {
    cocls::dispatcher::init();
    auto s = async_1();
    int i = cocls::dispatcher::await(s);
    std::cout << i << std::endl;
    
    
}
