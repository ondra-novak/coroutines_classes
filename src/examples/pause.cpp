//This example shows difference between synchronous and asychronous pause()

#include <iostream>
#include <coclasses/task.h>

cocls::task<> test_sync_pause(int id) {
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < id; i++) std::cout << "\t";
        std::cout << j << std::endl;        
        cocls::pause(); //synchronous pause
    }     
    co_return;
}


cocls::task<> sync_pause() {
    //cooperative mode need to be initialized in a coroutine.
    //The cooperative execution starts once coroutine exits
       for (int i = 0; i < 5; i++) {
           test_sync_pause(i);
       }
    co_return;
}

cocls::task<> test_async_pause(int id) {
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < id; i++) std::cout << "\t";
        std::cout << j << std::endl;        
        co_await cocls::pause(); //asynchronous pause
    }     
    co_return;
}


cocls::task<> async_pause() {
    //cooperative mode need to be initialized in a coroutine.
    //The cooperative execution starts once coroutine exits
       for (int i = 0; i < 5; i++) {
           test_async_pause(i);
       }
    co_return;
}



int main(int, char **) {
    std::cout<<"Async pause (co_await)" << std::endl;
    async_pause();
    std::cout<<"Sync pause (recursove)" << std::endl;
    sync_pause();
}
