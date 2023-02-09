#include <iostream>
#include <thread>
#include <coclasses/task.h>
#include <coclasses/future.h>


cocls::future<int> work() {
    return cocls::make_future<int>([](cocls::promise<int> p){
        std::thread thr([p = std::move(p)]() mutable {
            std::cout << "In a thread" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
           p.set_value(42);
        });
        thr.detach();
    });
}

cocls::coro_promise<int> cofn1() {
    cocls::future<int> fut;
    fut.result_of(work);
    co_return co_await fut;
}

cocls::future<int> api_fn() {
    return cocls::make_future(cofn1());
}

//task returning void
cocls::task<int> cofn2() {
    cocls::future<int> fut = api_fn();
    co_return co_await fut;
}





int main(int, char **) {
    std::cout << "Result:" << cofn2().join() <<std::endl;


}

