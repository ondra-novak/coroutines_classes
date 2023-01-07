#include <coclasses/generator.h>
#include <coclasses/future.h>

#include <coclasses/thread_pool.h>

#include <iostream>


cocls::generator<int> co_fib(cocls::thread_pool &pool, int count) {
    int a = 0;
    int b = 1;
    for(int i = 0;i < count; i++) {
        int c = a+b;        
        co_yield c;
        co_await pool;
        a = b;
        b = c;
    }
}


void test_cb(cocls::generator<int> &gen, cocls::promise<void> &&p) {
    gen >> [&,p = std::move(p)]() mutable {
        if (!gen.done()) {
            std::cout << gen.value() << std::endl;
            test_cb(gen,std::move(p));
        } else{
            std::cout << "Done" << std::endl;
            p.set_value();
        }        
    };
}

int main(int, char **) {

    cocls::thread_pool pool(2);
    auto gen = co_fib(pool, 20);
    cocls::future<void> f;
    test_cb(gen, f.get_promise());;
    f.wait();
    
}

