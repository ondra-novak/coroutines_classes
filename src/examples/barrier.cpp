/** @file barrier.cpp
 * 
 * Demonstration of a barrier. The barrier is setup to be released when
 * certain coroutines is awaiting on it. When the last coroutine performs co_await (desired
 * count of awaiting is reached), all awaiting coroutines are resumed
 * 
 * barrier can be also released manually 
 */


#include <iostream>
#include <coclasses/barrier.h>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>

#include <memory>

//


cocls::task<void, cocls::thread_pool_resumption_policy> print_thread_task(int i, cocls::barrier &b) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200*i));
    std::cout << "Task "<< i << " thread " << std::this_thread::get_id() << std::endl;
    co_await b;
    std::this_thread::sleep_for(std::chrono::milliseconds(10*i));
    std::cout << "Task "<< i << " thread " << std::this_thread::get_id() << std::endl;
    co_return;
}



int main(int, char **) {
    std::vector<cocls::task<> > tasks;
    cocls::barrier b(8);
    {
        auto pool = std::make_shared<cocls::thread_pool>(8);
        for (int i = 0; i < 8; i++) {
            auto t = print_thread_task(i, b); 
            t.initialize_policy(pool); //start this task now on the thread pool
            tasks.push_back(t);
        }
    }
    
    for (auto &t: tasks) {
        t.join();
    }
   
    
}


