#include <coclasses/generator.h>

#include <iostream>

cocls::generator<int> co_fib2(int count) {
    int a = 0;
    int b = 1;
    for(int i = 0;i < count; i++) {
        int c = a+b;        
        co_yield c;
        a = b;
        b = c;
    }
}


int main(int , char **) {

    auto fib2 = co_fib2(15);
    for (int &i: fib2) {
        std::cout << i << std::endl;;
    }

    return 0;
}
