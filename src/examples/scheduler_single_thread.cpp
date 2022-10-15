#include <iostream>
#include <coclasses/task.h>
#include <coclasses/scheduler.h>

cocls::task<> test_co(cocls::scheduler<> &sch) {
    std::cout << "test sleeps 500ms" << std::endl;
    co_await sch.sleep_for(std::chrono::milliseconds(500));
    std::cout << "test sleeps 2s"  << std::endl;
    co_await sch.sleep_for(std::chrono::seconds(2));
    std::cout << "done"  << std::endl;
}


int main(int, char **) {
    ///initialize scheduler
    cocls::scheduler<> sch;
    ///start the task
    auto task = test_co(sch);
    ///run scheduler until task finishes 
    sch.start(task);

}
