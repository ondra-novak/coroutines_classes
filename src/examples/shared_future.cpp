#include <iostream>
#include <thread>
#include <coclasses/task.h>
#include <coclasses/future.h>


cocls::future<int> work(int val, int time) {
    return [=](cocls::promise<int> p){
        std::thread thr([val, time, p = std::move(p)]() mutable {
            std::cout << "In a thread" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(time));
           p(val);
        });
        thr.detach();
    };
}

//task returning void
cocls::coro_future<int> dropped_coro() {
    int res = co_await work(10,500);
    std::cout << "Dropped coro returns " << res << std::endl;
    co_return res;
}

//task returning void
cocls::coro_future<int> normal_coro() {
    int res = co_await work(20,1000);
    std::cout << "Normal coro returns " << res << std::endl;
    co_return res;
}

cocls::coro_future<int> waiter1(cocls::shared_future<int> f) {
    int res = co_await f;
    std::cout << "Waiter 1 returns " << res << std::endl;
    co_return res;
}

cocls::coro_future<int> waiter2(cocls::shared_future<int> f) {
    int res = co_await f;
    std::cout << "Waiter 2 returns " << res << std::endl;
    co_return res;
}


int main(int, char **) {
    {
        cocls::shared_future<int> x(dropped_coro());
        // x is dropped
    }
    {
        cocls::shared_future<int> x(normal_coro());
        cocls::future(waiter1(x)).join();
        cocls::future(waiter2(x)).join();
    }
}


template class cocls::shared_future<int>;
template class cocls::shared_future<void>;
