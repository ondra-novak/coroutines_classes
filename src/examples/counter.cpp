#include <iostream>
#include <coclasses/counter.h>
#include <coclasses/task.h>




cocls::task<> coro(cocls::Counter &x, long val) {
        std::cout << "Awaiting counter - value: " << val << std::endl;
        co_await x;
        std::cout << "Coro continues - value " << val << std::endl;
}




int main(int, char **) {    
    cocls::Counter x(10);
    
    long lv = x.get_value();
    for (int i = 0; i < 20; i++) {        
        coro(x, lv);
        lv = --x;
        std::cout<<"Counter value:" << lv << std::endl;
    }    
    
}
