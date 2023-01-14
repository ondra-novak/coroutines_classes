#include <iostream>
#include <coclasses/task.h>
#include <coclasses/queue.h>
#include <coclasses/thread_pool.h>


cocls::task<> queue_task(cocls::queue<void> &q) {
    try {
        while(true) {
            co_await q.pop();
            std::cout<<"Received event from queue(void) " << std::endl;
        }
    } catch (const cocls::await_canceled_exception &e) {
        std::cout<<"Queue destroyed " << std::endl;
    }
 }

auto queue_test() {
    cocls::queue<void> q;
    auto task = queue_task(q);
    q.push();
    q.push();
    q.push();
    q.push();
    return task;    
}


int main(int, char **) {
    queue_test().join();     
    return 0;
}
