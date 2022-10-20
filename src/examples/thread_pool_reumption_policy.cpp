/** @file thread_pool_resumption_policy
 * 
 * Demonstrates using thread_pool_resumption_policy, which allows to bind a thread pool to a task. The
 * task is then resumed in this thread pool. 
 * 
 * Because there is no way how to initialize policy directly, it must be initialized by a function 
 * initialize_policy. Until this happen, the task is suspended. Once the task has a thread pool associated,
 * then it is started
 * 
 * So the function initialize_policy actually starts the task
 * 
 */
#include <iostream>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>

#include <memory>



cocls::task<void, cocls::thread_pool_resumption_policy> print_thread_task(int i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100*i));
    std::cout << "Task "<< i << " thread " << std::this_thread::get_id() << std::endl;    
    co_return;
}



int main(int, char **) {
    auto pool = std::make_shared<cocls::thread_pool>(8);
    std::vector<cocls::task<> > tasks;
    for (int i = 0; i < 8; i++) {
        auto t = print_thread_task(i); 
        t.initialize_policy(pool); //start this task now on the thread pool
        tasks.push_back(t);
    }
    pool = nullptr;
    for (auto &t: tasks) {
        t.join();
    }
   
    
}
