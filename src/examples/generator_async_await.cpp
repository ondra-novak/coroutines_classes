#include <coclasses/generator.h>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>

#include <iostream>

cocls::generator<int> co_fib(cocls::thread_pool &pool) {
    int a = 0;
    int b = 1;
    for(;;) {
        int c = a+b;        
        co_yield c;        
        co_await pool;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        a = b;
        b = c;
    }
}

cocls::task<> co_reader(cocls::generator<int> gen) {
    for (int i = 0; i < 20; i++) {
        bool b = co_await gen.next();
        if (b) {
            std::cout << gen.value() << std::endl;
        } else {
            std::cout << "Done" << std::endl;
        }
    }
    
}

int main(int, char **) {

    cocls::thread_pool pool(4);
    auto task = co_reader(co_fib(pool));
    task.join();
    
}

