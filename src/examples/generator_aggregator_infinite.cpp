#include <coclasses/generator.h>
#include <coclasses/generator_aggregator.h>

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


int main(int, char **) {

    std::vector<cocls::generator<int> > gens;
    gens.push_back(co_fib());
    gens.push_back(co_fib());
    gens.push_back(co_fib());
    auto gen = cocls::generator_aggregator(std::move(gens));
    
    for (int i = 0; i < 20; i++) {
        auto val = gen();
        if (val) {
            std::cout << *val << std::endl;
        } else {
            std::cout << "Done" << std::endl;
        }
    }
    
}

