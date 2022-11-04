/**
 * @file dispatcher_timers.cpp 
 * 
 * demonstration of usage of dispatcher's timer support (without scheduler)  
 */

#include <iostream>
#include <coclasses/task.h>
#include <coclasses/dispatcher.h>

cocls::task<void, cocls::resumption_policy::dispatcher> test_co() {
    std::cout << "test1 sleeps 500ms" << std::endl;
    co_await cocls::dispatcher::sleep_for(std::chrono::milliseconds(500));
    std::cout << "test1 sleeps 2s"  << std::endl;
    co_await cocls::dispatcher::sleep_for(std::chrono::seconds(2));
    std::cout << "test1 done"  << std::endl;
}

cocls::task<void, cocls::resumption_policy::dispatcher> test2_co() {
    std::cout << "test2 sleeps 250ms" << std::endl;
    co_await cocls::dispatcher::sleep_for(std::chrono::milliseconds(250));
    std::cout << "test2 sleeps 1s"  << std::endl;
    co_await cocls::dispatcher::sleep_for(std::chrono::seconds(1));
    std::cout << "test2 done"  << std::endl;
}


int main(int, char **) {
    ///initialize dispatcher on current thread
    cocls::dispatcher::init();
    ///start the task
    auto task = test_co();
    ///start the task2
    auto task2 = test2_co();
    ///await on task while running dispatcher on the thread 
    cocls::dispatcher::await(task);

}
