#include <coclasses/task.h>
#include <coclasses/parallel_resumption_policy.h>
#include <iostream>
#include <thread>



cocls::task<void, cocls::resumption_policy::parallel> print_thread_task(int i) {
    std::this_thread::sleep_for(std::chrono::seconds(i));
    std::cout << "Task "<< i << " thread " << std::this_thread::get_id() << std::endl;    
    co_return;
}


cocls::task<> normal_task(cocls::task<> t1) {
    co_await t1;
    std::cout << "Normal task thread " << std::this_thread::get_id() << std::endl;
    co_return;
}


int main(int, char **) {
    std::cout << "This thread: " << std::this_thread::get_id() << std::endl;
    
    auto t1 = print_thread_task(1);
    auto t2 = print_thread_task(2);
    auto t3 = normal_task(t1);
    t1.join();
    t2.join();
    t3.join();
    
    return 0;
}
