#include <coclasses/generator.h>
#include <coclasses/thread_pool.h>
#include <coclasses/generator_aggregator.h>

#include <iostream>

cocls::generator<int> co_fib(cocls::thread_pool &pool, int count, int delay) {
    int a = 0;
    int b = 1;
    for(int i = 0;i<count;i++) {
        int c = a+b;        
        co_yield c;
        co_await pool;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        a = b;
        b = c;
    }
}

int main(int, char **) {

    cocls::thread_pool pool(5);
    std::vector<cocls::generator<int> > gens;
    gens.push_back(co_fib(pool, 10, 200));
    gens.push_back(co_fib(pool, 20, 100));
    gens.push_back(co_fib(pool, 30, 50));
    auto gen = cocls::generator_aggregator(std::move(gens));
    for(;;) {
        std::optional<int> val = gen();
        if (val.has_value()) {
            std::cout << *val << std::endl;
        } else {
            std::cout << "Done" << std::endl;
            break;
        }
        
    }
}

