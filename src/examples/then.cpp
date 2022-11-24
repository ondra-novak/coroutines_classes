/**
 * @file then.cpp
 * 
 * Demonstration of then() - transform of result
 */

#include <iostream>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>


//task returning void
cocls::task<> cofn1(cocls::thread_pool &p) {
    std::cout << "cofn1 - thread - " << std::this_thread::get_id() << std::endl;
    //transfer this coroutine into thread pool
    co_await p;
    std::cout << "cofn1 - thread - " << std::this_thread::get_id() << std::endl;    
}


//task returning int
cocls::task<int> cofn2(cocls::thread_pool &p) {
    std::cout << "cofn2 - await " << std::endl;
    co_await cofn1(p);
    std::cout << "cofn2 - resumes " << std::endl;
    co_return 42;
}



int main(int, char **) {
    cocls::thread_pool p(1);
    //run task, wait for result
    int ret = cofn2(p).then(
            [](int i){return i*2;} // transforms result
         ).join();
    std::cout << "Result:" << ret <<std::endl;
    
    
}

