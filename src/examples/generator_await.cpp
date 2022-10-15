#include <coclasses/generator.h>
#include <coclasses/task.h>

#include <iostream>

cocls::generator<int> co_fib() {
    int a = 0;
    int b = 1;
    for(;;) {
        int c = a+b;        
        co_yield c;
        a = b;
        b = c;
    }
}

cocls::task<> co_reader(cocls::generator<int> &&gen) {
    for (int i = 0; i < 20; i++) {
        std::optional<int> val = co_await gen;
        if (val.has_value()) {
            std::cout << *val << std::endl;
        } else {
            std::cout << "Done" << std::endl;
        }
    }
    
}

int main(int, char **) {

    auto task = co_reader(co_fib());
    task.join();
    
}

