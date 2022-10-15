#include <iostream>
#include <coclasses/task.h>
#include <coclasses/with_queue.h>
#include <coclasses/thread_pool.h>


using queued_task = cocls::with_queue<cocls::task<void>, int>; 
 queued_task with_queue_task() {
        int i = co_await queued_task::current_queue();
        while (i) {
            std::cout<<"Received from queue: " << i << std::endl;
            i = co_await queued_task::current_queue();
        }
        std::cout<<"Done" << std::endl;        
}

void with_queue_test() {
    cocls::with_queue<cocls::task<void>, int> wq = with_queue_task();
    wq.push(1);
    wq.push(2);
    wq.push(3);
    wq.push(0);
    wq.join();    
}


int main(int, char **) {
    with_queue_test();
     
     return 0;
}
