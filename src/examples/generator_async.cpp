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
//        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        a = b;
        b = c;
    }
}

void reader(cocls::generator<int> gen) {
    for (int i = 0; i < 20; i++) {
        std::optional<int> val = gen();
        if (val.has_value()) {
            std::cout << *val << std::endl;
        } else {
            std::cout << "Done" << std::endl;
        }
    }
    
}

int main(int, char **) {

    cocls::thread_pool pool(4);
    reader(co_fib(pool));    
    
}

