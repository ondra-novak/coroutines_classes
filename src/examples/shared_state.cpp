/** @file shared_state.cpp
 * 
 *  Demonstration of shared state. You can await on this state. 
 *  Shared  state works like ref-counted pointer. You pass
 *  the state to every task, and access it there. It also
 *  contains a mutex, to lock the state for accessing. You
 *  can await the state. Awaiting it decreases reference
 *  count and once the reference count reaches zero,
 *  the awaiting coroutine is resumed and automatically
 *  aquires reference. Then it is able to retrieve results
 *   
 * 
 */
#include <iostream>
#include <coclasses/shared_state.h>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>
#include <iostream>


struct my_state {
    int _var = 0;
    std::vector<int> _regs; 
};

cocls::task<void, cocls::resumption_policy::thread_pool> print_thread_task(int i, cocls::shared_state<my_state> st) {
    //simulates work
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    //lock shared state
    auto ownership = co_await st.lock();
    //store results
    st->_var++;
    st->_regs.push_back(i);
    //all done
    co_return;
}

cocls::task<> waiter(cocls::shared_state<my_state> st) {
    my_state &res = co_await st;
    std::cout << "_var: " << res._var << std::endl;
    std::cout << "_regs:";
    for(auto &x: res._regs) {std::cout << " " << x ;}
    std::cout << std::endl;    
}


int main(int, char **) {
    auto st = cocls::make_shared_state<my_state>({});
    
    {
        auto pool = std::make_shared<cocls::thread_pool>(8);
        for (int i = 0; i < 8; i++) {
            auto t = print_thread_task(i, st); 
            t.initialize_policy(pool); //start this task now on the thread pool
            
        }
    }
    
    waiter(std::move(st)).join();  //need std::move to avoid keeping reference in main()
    
    
    
   
}
