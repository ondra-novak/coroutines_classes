#include <iostream>
#include <coclasses/task.h>
#include <coclasses/cancelable.h>
#include <coclasses/thread_pool.h>
#include <coclasses/scheduler.h>


cocls::cancelable<cocls::lazy<int> > task_example(cocls::scheduler<> &sch) {
    std::cout << "Running long task. Hit enter to cancel (20 sec)"<< std::endl;;
    for (int i = 0; i < 20; i++) {
        co_await sch.sleep_for(std::chrono::seconds(1));
        std::cout << "Cycle: " << i << std::endl;
    }
    std::cout << "Task finished"<< std::endl;;
    co_return 42;
}

int main(int, char **) {
    cocls::thread_pool pool(1);
    cocls::scheduler<> sch(pool);
    auto t = task_example(sch);
    sch.start_after(t, std::chrono::seconds(2));
    std::cin.get();
    t.cancel();
    try {
        int i = t.join();
        std::cout << "Task was not cancelled, return value is " << i << std::endl;
    } catch (const cocls::await_canceled_exception &) {
        std::cout << "Canceled!" << std::endl;
    }
     return 0;
}
