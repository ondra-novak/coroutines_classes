#include <coclasses/task.h>
#include <coclasses/future.h>
#include <coclasses/lazy.h>
#include <coclasses/generator.h>

#include <iostream>
#include <assert.h>

cocls::lazy<int> co_lazy() {
    std::cout << "(co_lazy) executed" << std::endl;
    co_return 56;
}

cocls::task<int> co_test() {
    std::cout << "(co_test) started" << std::endl;    
    cocls::future<int> f;
    f >> [](cocls::future<int> &x) {
        std::cout << "(co_test) future's callback called: " << x.get() << std::endl;
    };
    auto cbp = cocls::make_promise<int>([](cocls::future<int> &x){
       std::cout << "(make_promise) called:" << x.get() << std::endl; 
    });
    std::thread thr([p = f.get_promise(), p2 = cbp]{
        std::cout << "(co_test) thread started" << std::endl;    
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "(co_test) promise being set" << std::endl;  
        p.set_value(42);
        p2.set_value(78);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "(co_test) thread finished" << std::endl;
    });
    thr.detach();
    std::cout << "(co_test) await" << std::endl;
    int i = co_await(f);
    std::cout << "(co_test) await finished i = " << i << std::endl;
    co_return(i);
}

cocls::task<int> co_test2() {
    std::cout << "(co_test2) co_lazy() started" << std::endl;
    cocls::lazy<int> lz = co_lazy();
    std::cout << "(co_test2) await" << std::endl;
    int i = co_await(co_test());
    int j = co_await lz;
    std::cout << "(co_test2) await finished i = " << i<< ", j = " << j << std::endl;   
    co_return(i);
}

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

template class cocls::future<void>;

int main(int argc, char **argv) {
    std::cout << "(main) starting co_test2" << std::endl;    
    auto z = co_test2();
    std::cout << "(main) waiting for future" << std::endl;
    std::cout << z.wait() << std::endl;
    
    auto fib = co_fib();
    std::cout<< "gen1: ";    
    for (int i = 0; i < 15; i++) {
        auto iter = fib.begin();
        if (iter != fib.end()) {
            std::cout << *iter << " " ;
        }
    }
    std::cout<< std::endl;

    auto fib2 = co_fib2(15);
    std::cout<< "gen2: ";
    for (int &i: fib2) {
        std::cout << i << " ";
    }
    std::cout<< std::endl;

    auto fib3 = co_fib2(15);
    std::cout<< "gen3: ";
    while (!!fib3) {
        std::cout << fib3() << " ";
    }
    std::cout<< std::endl;

    
}
