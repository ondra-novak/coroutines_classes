#include <iostream>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>


void __attribute__((optimize("O0"))) breakpoint_place() {
    (void)std::cin.eof(); //dummy
}


cocls::task<int, cocls::resumption_policy::immediate> coro1(cocls::thread_pool &thread) {
    std::cout << "coro1: enter thread " << std::this_thread::get_id() << std::endl;
    co_await thread;
    breakpoint_place();
    std::this_thread::sleep_for(std::chrono::seconds(1));    
    std::cout << "coro1: exit thread " << std::this_thread::get_id() << std::endl;
    co_return 42;
}

cocls::task<int, cocls::resumption_policy::immediate> coro2(cocls::task<int> future) {
    std::cout << "coro2: enter thread " << std::this_thread::get_id() << std::endl;
    int i = co_await future;
    std::cout << "coro2: exit thread " << std::this_thread::get_id() << std::endl;
    co_return i*2;
}


int main(int, char **) {
    
    cocls::thread_pool thread(1);
    auto t1 = coro1(thread);
    auto t2 = coro2(t1);
    int v1 = t1.join(); //two awaits - join + coro2 
    std::cout << "V1: " << v1 << std::endl;
    //coro2 is resumed using symmetric transfer
    int v2 = t2.join();
    std::cout << "V2: " << v2 << std::endl;
    
    
}
