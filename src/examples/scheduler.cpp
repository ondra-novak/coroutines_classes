#include <iostream>
#include <coclasses/task.h>
#include <coclasses/scheduler.h>
#include <coclasses/thread_pool.h>

cocls::task<> test_co(cocls::scheduler<> &sch) {
    std::cout << "test sleeps 500ms" << std::endl;
    co_await sch.sleep_for(std::chrono::milliseconds(500));
    std::cout << "test sleeps 2s"  << std::endl;
    co_await sch.sleep_for(std::chrono::seconds(2));
    std::cout << "done"  << std::endl;
}


int main(int, char **) {
    cocls::thread_pool pool(1);
    cocls::scheduler<> sch(pool);
    test_co(sch).join();

}
