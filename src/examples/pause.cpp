/** @file pause.cpp
 * 
 * Demonstration of cocls::pause() - pauses current task and resumes it under
 * different resumption policy. Default resumption policy is queued_resumption_policy
 * which causes that task is queued and then resumed after all tasks queued before are
 * finished. But this function can be used to temporarily enforce resumption policy.
 * 
 * If the specified resumption policy is thead_pool, then current task is resumed under that 
 * thread_pool
 * 
 * If the specufued resumption policy is parallel_resumption_policy, then task 
 * continues in new thread allowing the current thread run in parallel.
 * 
 */

#include <coclasses/task.h>
#include <coclasses/parallel_resumption_policy.h>

#include <thread>
#include <iostream>

cocls::task<> test_task() {
    std::cout << "cur thread " << std::this_thread::get_id() << std::endl;
    co_await cocls::pause<cocls::resumption_policy::parallel>();
    std::cout << "cur thread " << std::this_thread::get_id() << std::endl;
    co_return;
}




int main(int, char **) {
    test_task().join();
    
}
