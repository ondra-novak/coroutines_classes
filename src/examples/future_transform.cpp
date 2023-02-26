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
cocls::future<int> cofn1() {
    cocls::future<int> fut;
    fut << work;
    co_return co_await fut;
}


//use transform
void example1() {
    auto hello_world = [](std::string s) {return "Hello world: " + s;};

    auto int_to_string_transform = [&](auto fn){
        return cocls::transform(static_cast<std::string (*)(int)>(&std::to_string), fn);
    };
    auto hello_world_transform = [&](auto fn) {
        return cocls::transform(hello_world, fn);
    };
    auto s = hello_world_transform([&]{return int_to_string_transform(cofn1);});

    auto r = s.join();
    std::cout << "Result:" << r <<std::endl;
}

//use transform_inline
void example2() {
    auto hello_world = [](std::string s) {return "Hello world: " + s;};

    //transform int to string from function returning future
    auto int_to_string_transform = [&](auto fn){
        return cocls::transform_inline(static_cast<std::string (*)(int)>(&std::to_string), fn);
    };
    auto hello_world_transform = [&](auto fn) {
        return cocls::transform_inline(hello_world, fn);
    };
    //inline transform - no allocation, returned type is derived from future<>
    //first is transform function, second is function returning future to transform
    auto s = hello_world_transform([&]{return int_to_string_transform(cofn1);});

    auto r = s.join();
    std::cout << "Result:" << r <<std::endl;
}

int main(int, char **) {
    example1();
    example2();
}
