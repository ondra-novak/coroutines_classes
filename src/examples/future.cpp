#include <iostream>
#include <thread>
#include <coclasses/task.h>
#include <coclasses/future.h>

cocls::future<int> work() {
    return [](cocls::promise<int> p){
        std::thread thr([p = std::move(p)]() mutable {
            std::cout << "In a thread" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
           p(42);
        });
        thr.detach();
    };
}

//task returning void
cocls::task<int> cofn1() {
    cocls::future<int> fut;
    fut << work;
    co_return co_await fut;
}

int main(int, char **) {
    std::cout << "Result:" << cofn1().join() <<std::endl;
}
