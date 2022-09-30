#include "coclasses.h"

#include <iostream>
#include <assert.h>


cocls::task<int> co_test() {
    std::cout << "(co_test) started" << std::endl;    
    cocls::callback_promise<int> cbprom;
    std::thread thr([cb = cbprom.get_callback()]{
        std::cout << "(co_test) thread started" << std::endl;    
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "(co_test) callback called" << std::endl;  
        cb(42);
        std::cout << "(co_test) thread finished" << std::endl;
    });
    thr.detach();
    std::cout << "(co_test) await" << std::endl;
    int i = co_await(cbprom);
    std::cout << "(co_test) await finished i = " << i << std::endl;
    co_return(i);
}

cocls::task<int> co_test2() {
    std::cout << "(co_test2) await" << std::endl;   
    int i = co_await(co_test());
    std::cout << "(co_test2) await finished i = " << i  << std::endl;   
    co_return(i);
}

int main(int argc, char **argv) {
    std::cout << "(main) starting co_test2" << std::endl;
    auto z = co_test2();
    auto f = z.get_future();
    std::cout << "(main) waiting for future" << std::endl;
    std::cout << f.get() << std::endl;
    
}
