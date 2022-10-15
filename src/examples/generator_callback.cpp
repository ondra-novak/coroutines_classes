#include <coclasses/generator.h>
#include <coclasses/task.h>

#include <iostream>

cocls::generator<int> co_fib(int count) {
    int a = 0;
    int b = 1;
    for(int i = 0;i < count; i++) {
        int c = a+b;        
        co_yield c;
        a = b;
        b = c;
    }
}


void test_cb(cocls::generator<int> &gen) {
    gen >> [&](std::optional<int> val) {
        if (val.has_value()) {
            std::cout << *val << std::endl;
            test_cb(gen);
        } else{
            std::cout << "Done" << std::endl;
        }        
    };
}

int main(int, char **) {

    auto gen = co_fib(20);
    test_cb(gen);
    
}

