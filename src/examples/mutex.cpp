#include <iostream>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>
#include <coclasses/mutex.h>
#include <coclasses/scheduler.h>

cocls::task<> test_task(cocls::scheduler<> &sch, cocls::mutex &mx, int &shared_var) {
    co_await sch.pause();
    auto lk = co_await mx.lock();
    std::cout << "Mutex acquired" << std::endl;
    shared_var++;
    std::cout << "Shared var increased under mutex: " << shared_var << std::endl;
    co_await sch.sleep_for(std::chrono::milliseconds(100));
    std::cout << "Mutex released " << std::endl;
    co_return;
}

int main(int, char **) {
    cocls::mutex mx;
    cocls::thread_pool pool(5);
    cocls::scheduler<> sch(pool);
    int shared_var = 0;
    std::vector<cocls::task<> > tasks;
    for (int i = 0; i < 5; i++) {
        tasks.push_back(test_task(sch, mx, shared_var));
    }
    for (cocls::task<> &x: tasks) x.join();
    return 0;

}
