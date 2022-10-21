#include <coclasses/lazy.h>
#include <coclasses/parallel_resumption_policy.h>
#include <coclasses/dispatcher.h>
#include <coclasses/future.h>
#include <iostream>
#include <thread>


template<typename T>
using disp_task = cocls::task<T, cocls::resumption_policy::dispatcher>;


cocls::lazy<int> delay() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    co_return 1;
}

disp_task<void> task1() {
    std::cout << "Task 1" << std::endl;
    co_return;
}

disp_task<int> task2(cocls::task<int> t) {
    std::cout << "Task 2" << std::endl;    
    co_return co_await t;
}

int main(int, char **) {
    
    auto l = delay();
    auto h = l.get_start_handle();
    cocls::future<void> f;
    
    
    std::thread thr1([l, p = f.get_promise()]{
        cocls::dispatcher::init();
        auto t1 = task1();
        disp_task<int> t2 = task2(cocls::lazy<int> (l));
        std::thread thr2([t2,p = std::move(p)]() mutable {
            try {
                int v = t2.join(); //join in other thread
                std::cout << "Result :" << v << std::endl; //should not called
            } catch (std::exception &e) {
                std::cout << "Exception :" << e.what() << std::endl; //should not called
            }
            p.set_value();            
        });
        thr2.detach();
        cocls::dispatcher::await(t1);
        //dispatcher thread ends here before t2 is resumed
    });
    
    thr1.detach();
    h.resume();
    f.wait();
    
    return 0;
    


}
