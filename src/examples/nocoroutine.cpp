/** @file nocoroutine.cpp
 * 
 * Demonstration how you can create task<> with already resolved value, so you
 * don't need coroutine to do this
 * 
 */

#include <iostream>
#include <cassert>
#include <coclasses/task.h>

template <typename T> 
__attribute__((optimize("O0"))) inline void doNotOptimizeAway(T&& x)  {
    (void)x;
}

cocls::task<> co_main() {
    auto always_ready = cocls::task<void>::set_result();
    
    co_await always_ready;  //never suspend here

    auto task_true = cocls::task<bool>::set_result(true);
    auto task_false = cocls::task<bool>::set_result(false);
    
    bool b1 = co_await task_true;
    bool b2 = co_await task_false;
    
    assert(b1 == true);
    assert(b2 == false);
    
    auto preinit = cocls::task<int>::set_result(42); //initialize directly by result
    
    int r = co_await preinit;  //co_await this task
    
    
    std::cout<<r<<std::endl;
    std::cout<<b1<<std::endl;
    std::cout<<b2<<std::endl;
    
    co_return;
}



int main(int argc, char **argv) {

    co_main().join();
    
}
