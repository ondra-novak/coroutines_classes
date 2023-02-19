#include <coclasses/generator.h>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>

#include <iostream>

cocls::generator<int> co_fib(cocls::thread_pool &pool) {
    int a = 0;
    int b = 1;
    for(int i = 0; i < 10; i++) {
        int c = a+b;
        co_yield c;
        co_await pool;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        a = b;
        b = c;
    }
    throw std::runtime_error("Exception thrown");
}

cocls::task<> reader(cocls::generator<int> gen) {
    try {
        auto fut = gen();
        while (co_await fut.has_value()) {
            std::cout << *fut << std::endl;
            fut.result_of(gen);
        }
    } catch (std::exception &e) {
        std::cout << "Exception hit: " << e.what() << std::endl;
    }
}

int main(int, char **) {

    cocls::thread_pool pool(4);
    reader(co_fib(pool)).join();

}

