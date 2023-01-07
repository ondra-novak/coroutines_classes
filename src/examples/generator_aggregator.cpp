#include <coclasses/generator_aggregator.h>

#include <coclasses/generator.h>

#include <iostream>

cocls::generator<int> co_fib(int count) {
    int a = 0;
    int b = 1;
    for(int i = 0;i<count;i++) {
        int c = a+b;        
        co_yield c;
        a = b;
        b = c;
    }
}


int main(int, char **) {

    std::vector<cocls::generator<int> > gens;
    gens.push_back(co_fib(10));
    gens.push_back(co_fib(20));
    gens.push_back(co_fib(30));
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

