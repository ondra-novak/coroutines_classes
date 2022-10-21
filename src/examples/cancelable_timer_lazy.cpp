#include <iostream>
#include <coclasses/thread_pool.h>
#include <coclasses/scheduler.h>


cocls::lazy<int> lazytask() {
    std::cout << "Lazy coroutine started" << std::endl;
    co_return 42;
    
}


int main(int, char **) {
    cocls::thread_pool pool(1);
    cocls::scheduler<> sch(pool);

    auto t = lazytask();
    std::cout << "Hit ENTER to cancel timer (10sec)" << std::endl;
    sch.start_after(t, std::chrono::seconds(10));
    std::cin.get();
    sch.cancel(t);
    try {
        int val = t.join();
        std::cout << "Finished! Lazy coroutine returned " << val << std::endl;
    } catch (const cocls::await_canceled_exception &) {
        std::cout << "Canceled!" << std::endl;
    }
    return 0;
}
