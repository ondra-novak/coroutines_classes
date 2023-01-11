#include <iostream>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>
#include <coclasses/publisher.h>
#include <coclasses/scheduler.h>

cocls::task<> subscriber_fast(cocls::publisher<int> &pub) {
    cocls::subscriber<int> src(pub);
    while (co_await src.next()) {
        int x = src.value();
        std::cout<<"\t" << x << std::endl;
    }
    
}

cocls::task<> subscriber_slow(cocls::publisher<int> &pub, cocls::scheduler<> &sch) {
    cocls::subscriber<int> src(pub);
    while (co_await src.next()) {
        int x = src.value();
        std::cout<<"\t\t" << x << std::endl;
        co_await sch.sleep_for(std::chrono::milliseconds(100));
    } 
}

cocls::task<> subscriber_slow2(cocls::publisher<int> &pub, cocls::scheduler<> &sch) {
    cocls::subscriber<int> src(pub);
    while (co_await src.next()) {
        int x = src.value();
        std::cout<<"\t\t\t" << x << std::endl;
        co_await sch.sleep_for(std::chrono::milliseconds(200));
        while (src.next_ready()); //skip all old values
    } 
}

void subscriber_sync(cocls::publisher<int> &pub) {
    cocls::subscriber<int> src(pub,0);
    for(auto &x: src) {
        std::cout<<"\t\t\t\t" << x << std::endl;
    }
}


void publisher_test() {
    cocls::publisher<int> pub;
    cocls::thread_pool thp(10);
    cocls::scheduler<> sch(thp);
    
    auto s1 = subscriber_fast(pub);
    auto s2 = subscriber_slow(pub, sch);;
    auto s3 = subscriber_slow2(pub, sch);;
    auto s4 = std::thread([&]{
        subscriber_sync(pub);
    });
    for (int i = 0; i < 100; i++) {
        pub.publish(i);
        std::this_thread::sleep_for(std::chrono::milliseconds(200-i*2));
    }
    pub.close();
    s1.join();
    s2.join();
    s3.join();
    s4.join();
    
}


int main(int, char **) {
    publisher_test();
    return 0;
}
