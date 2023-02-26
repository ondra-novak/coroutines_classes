#include <iostream>
#include <coclasses/thread_pool.h>
#include <coclasses/scheduler.h>


cocls::task<> cancelable(cocls::scheduler &sch, cocls::scheduler::ident id) {
    std::cout << "Hit ENTER to cancel timer (10sec)" << std::endl;
    try {
        co_await sch.sleep_for(std::chrono::seconds(10), id);
        std::cout << "Finished!" << std::endl;
    } catch (const cocls::await_canceled_exception &) {
        std::cout << "Canceled!" << std::endl;
    }
    co_return;
}


int main(int, char **) {
    cocls::thread_pool pool(1);
    cocls::scheduler sch(pool);


    cocls::task<> t = cancelable(sch, &t);
    std::cin.get();
    sch.cancel(&t);
    t.join();
    return 0;
}
