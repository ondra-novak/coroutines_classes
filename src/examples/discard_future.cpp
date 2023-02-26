#include <iostream>
#include <thread>
#include <coclasses/task.h>
#include <coclasses/future.h>


cocls::future<int> work(std::thread &thr) {
    return [&](cocls::promise<int> p){
        thr = std::thread ([p = std::move(p)]() mutable {
            std::cout << "In a thread" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
           p(42);
        });
    };
}

//task returning void
cocls::future<int> cofn1(std::thread &thr) {
    cocls::future<int> fut;
    fut << [&]{return work(thr);};
    co_return co_await fut;
}

int main(int, char **) {
    std::thread thr;
    cocls::discard([&]{return cofn1(thr);});
    thr.join();
    std::cout << "Future discarded" << std::endl;


}
