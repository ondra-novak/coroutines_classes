/** @file multi_source_future.cpp
 * 
 * Demonstration of future, which can have multiple source
 * where only first source is used, others are ignored
 * 
 * This can be useful when we have two racing sources, for
 * example a calculation and a timeout. When the calculation is
 * slow, the timeout is reached and awaiting coroutine is resumed. After
 * that result of calculation is thrown out. The future object
 * is still able to accept values even if the coroutine is no longer
 * waiting. 
 * 
 */
#include <coclasses/task.h>
#include <coclasses/future.h>
#include <coclasses/parallel_resumption_policy.h>
#include <iostream>


cocls::task<void, cocls::resumption_policy::parallel> calc_val(int i, int delay, cocls::shared_promise<int> p) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    p.set_value(i);
    co_return;
}

cocls::task<int> collector(int i, int j, int d1, int d2) {
    cocls::future<int> fut;
    auto prom = fut.get_shared_promise();
    calc_val(i,d1, prom);
    calc_val(j,d2, prom);
    co_return co_await fut;
}



int main(int, char **) {
    std::cout<<"Test 1: " << collector(1,2,100,200).join() << std::endl; //first source wins 
    std::cout<<"Test 2: " << collector(1,2,200,100).join() << std::endl; //second source wins
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

