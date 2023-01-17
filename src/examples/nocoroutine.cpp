/** @file nocoroutine.cpp
 * 
 * Demonstration how you can create task<> with already resolved value, so you
 * don't need coroutine to do this
 * 
 */

#include <iostream>
#include <cassert>
#include <coclasses/task.h>

cocls::task<> co_main() {
    cocls::task<void> always_ready;
    
    co_await always_ready;  //never suspend here

    cocls::task<bool> task_true(true);//true and false are preallocated
    cocls::task<bool> task_false(false);
    
    assert(co_await task_true == true);
    assert(co_await task_false == false);
    
    cocls::task<int> preinit(42); //initialize directly by result
    
    int r = co_await preinit;  //co_await this task
    
    std::cout<<r<<std::endl;
    
    co_return;
}



int main(int argc, char **argv) {

    co_main().join();
    
}
