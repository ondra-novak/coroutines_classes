//example inspired by
//https://go.dev/tour/concurrency/1


#include <iostream>
#include <coclasses/task.h>
#include <coclasses/scheduler.h>


cocls::task<> say(cocls::scheduler<> &sch, std::string s) {
    for (int i = 0; i < 5; i++) {
        co_await sch.sleep_for(std::chrono::milliseconds(100));
        std::cout << s << std::endl;
    }
    co_return;
    
}

int main(int, char **) {
    cocls::scheduler<> sch;
    auto t1 = say(sch, "hello");
    auto t2 = say(sch, "world");
    sch.start(t1);
    sch.start(t2);
}
