#include <coclasses/generator.h>
#include <coclasses/thread_pool.h>
#include <coclasses/generator_aggregator.h>

#include <iostream>

cocls::generator<int> co_fib(cocls::thread_pool &pool, int delay) {
    int a = 0;
    int b = 1;
    for(;;) {
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
    gens.push_back(co_fib(pool,100));
    gens.push_back(co_fib(pool,200));
    gens.push_back(co_fib(pool,300));
    auto gen = cocls::generator_aggregator(std::move(gens));
    
    for (int i = 0; i < 40; i++) {
        auto val = gen();
        if (val) {
            std::cout << *val << std::endl;
        } else {
            std::cout << "Done" << std::endl;
        }
    }
    
}
