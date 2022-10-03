#include "../../version.h"
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

cocls::generator<int> co_async_fib(int count) {
    int a = 0;
    int b = 1;
    for(int i = 0;i < count; i++) {
        cocls::future<int> cf;
        std::thread thr([&a,&b,cp = cf.get_promise()]{
            int c = a+b;
            cp.set_value(c);
            a = b;
            b = c;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
        thr.detach();
        int c = co_await cf;
        co_yield c;
    }
}

cocls::task<void> co_fib_reader()  {
    std::cout<< "async gen - co_await: ";
    auto g = co_async_fib(15);
    while (co_await g) {
        std::cout << g() << " " ;
    }
    std::cout<< std::endl;
}
cocls::task<void> co_fib2_reader()  {
    std::cout<< "sync gen - co_await;";
    auto g = co_fib2(15);
    while (co_await g) {
        std::cout << g() << " " ;
    }
    std::cout<< std::endl;
}
cocls::task<void> co_fib3_reader()  {
    std::cout<< "sync gen - resume in coroutine: ";
    auto g = co_fib2(15);
    while (g) {
        std::cout << g() << " " ;
    }
    std::cout<< std::endl;
    co_return;
}

template class cocls::future<void>;

int main(int argc, char **argv) {
    std::cout << "MIT License Copyright (c) 2022 Ondrej Novak" << std::endl;
    std::cout << "Version: " << GIT_PROJECT_VERSION << std::endl;
    std::cout << std::endl;
    std::cout << "(main) starting co_test2" << std::endl;    
    auto z = co_test2();
    std::cout << "(main) waiting for future" << std::endl;
    std::cout << z.join() << std::endl;


    
    auto fib = co_fib();
    std::cout<< "infinite gen: ";    
    for (int i = 0; i < 15; i++) {
        auto iter = fib.begin();
        if (iter != fib.end()) {
            std::cout << *iter << " " ;
        }
    }
    std::cout<< std::endl;

    auto fib2 = co_fib2(15);
    std::cout<< "finite gen - range for: ";
    for (int &i: fib2) {
        std::cout << i << " ";
    }
    std::cout<< std::endl;

    auto fib3 = co_fib2(15);
    std::cout<< "finite gen - next/read: ";
    while (!!fib3) {
        std::cout << fib3() << " ";
    }
    std::cout<< std::endl;

    co_fib_reader().join();


    auto fib4 = co_async_fib(15);
    std::cout<< "async gen2 - next/read (sync): ";
    while (!!fib4) {
        std::cout << fib4() << " ";
    }
    std::cout<< std::endl;

    co_fib2_reader().join();
    
    co_fib3_reader().join();

    
}
