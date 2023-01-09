/**
 * @file detached.cpp
 *
 * demonstration of using detached coroutine
 */




#include <coclasses/dispatcher.h>
#include <coclasses/future.h>
#include <coclasses/detached.h>
#include <iostream>


cocls::detached<> coro_test(cocls::promise<int> prom) {
    std::cout << "coroutine is running" << std::endl;
    co_await cocls::dispatcher::sleep_for(std::chrono::seconds(1));
    std::cout << "coroutine is running... 2" << std::endl;
    co_await cocls::dispatcher::sleep_for(std::chrono::seconds(1));
    std::cout << "coroutine resolving promise" << std::endl;
    prom.set_value(42);
    std::cout << "coroutine finished" << std::endl;
}


int main(int, char **) {
    cocls::dispatcher::init();
    cocls::future<int> fut;
    coro_test(fut.get_promise());
    int result = cocls::dispatcher::await(fut);
    std::cout << "Result:" << result << std::endl;


}
