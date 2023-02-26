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

int main(int, char **) {

    auto gen = co_fib(20);
    auto val = gen();
    while (val) {
        std::cout << *val << std::endl;
        val << [&]{return gen();}; //reassign to future - call generator
    }
    std::cout << "Done" << std::endl;

}

