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

cocls::async<int> cofn1() {
    cocls::future<int> fut;
    fut.result_of(work);
    co_return co_await fut;
}

cocls::future<int> api_fn() {
    return cofn1();
}

//task returning void
cocls::async<int> cofn2() {
    cocls::future<int> fut = api_fn();
    co_return co_await fut;
}

int main(int, char **) {
    std::cout << "Result:" << cocls::future(cofn2()).join() <<std::endl;


}

