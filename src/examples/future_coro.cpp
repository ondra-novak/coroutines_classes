#include <iostream>
#include <thread>
#include <coclasses/task.h>
#include <coclasses/future.h>


cocls::future<int> work() {
    return [](cocls::promise<int> p){
        std::thread thr([p = std::move(p)]() mutable {
            std::cout << "In a thread" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
           p.set_value(42);
        });
        thr.detach();
    };
}

cocls::future<int> cofn1() {
    cocls::future<int> fut;
    fut << work;
    co_return co_await fut;
}


//task returning void
cocls::future<int> cofn2() {
    cocls::future<int> fut = cofn1();
    co_return co_await fut;
}

int main(int, char **) {
    std::cout << "Result:" << cofn2().wait() <<std::endl;


}

