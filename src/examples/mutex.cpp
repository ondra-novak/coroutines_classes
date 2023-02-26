#include <forward_list>
#include <iostream>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>
#include <coclasses/mutex.h>
#include <coclasses/scheduler.h>

cocls::async<void> test_task(cocls::scheduler &sch, cocls::mutex &mx, int &shared_var) {
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
    cocls::scheduler sch(pool);
    int shared_var = 0;
    std::forward_list<cocls::future<void> > tasks;
    for (int i = 0; i < 5; i++) {
        tasks.emplace_front([&]{
            return pool.run(test_task(sch, mx, shared_var));
        });
    }
    for (auto &x: tasks) x.join();
    return 0;

}
