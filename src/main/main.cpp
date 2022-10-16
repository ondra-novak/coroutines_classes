#include "../../version.h"
#include <coclasses/generator.h>
#include <coclasses/generator_aggregator.h>
#include <coclasses/task.h>
#include <coclasses/future.h>
#include <coclasses/lazy.h>
#include <coclasses/mutex.h>
#include <coclasses/queue.h>
#include <coclasses/thread_pool.h>
#include <coclasses/scheduler.h>
#include <coclasses/with_queue.h>
#include <coclasses/abstract_awaiter.h>
#include <coclasses/no_alloc.h>
#include <coclasses/publisher.h>
#include <coclasses/sync_await.h>
#include <array>
#include <iostream>
#include <cassert>
#include <random>




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

cocls::generator<int> co_async_fib(int count, int delay = 100) {
    int a = 0;
    int b = 1;
    for(int i = 0;i < count; i++) {
        cocls::future<int> cf;
        std::thread thr([&a,&b,&delay, cp = cf.get_promise()]{
            int c = a+b;
            cp.set_value(c);
            a = b;
            b = c;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        });
        thr.detach();
        int c = co_await cf;
        co_yield c;
    }
}

cocls::task<void> co_fib_reader()  {
    std::cout<< "async gen - co_await: ";
    auto g = co_async_fib(15);
    while (co_await g.next()) {
        std::cout << g.value() << " " ;
    }
    std::cout<< std::endl;
}
cocls::task<void> co_fib2_reader()  {
    std::cout<< "sync gen - co_await;";
    auto g = co_fib2(15);
    while (co_await g.next()) {
        std::cout << g.value() << " " ;
    }
    std::cout<< std::endl;
}
cocls::task<void> co_fib3_reader()  {
    std::cout<< "sync gen - resume in coroutine: ";
    auto g = co_fib2(15);
    while (g.next()) {
        std::cout << g.value() << " " ;
    }
    std::cout<< std::endl;
    co_return; //need co_return to have this as coroutine
}
cocls::task<void> co_multfib_reader()  {
    std::cout<< "async gen multi - co_await: ";
    std::vector<cocls::generator<int> > glist;
    glist.push_back(co_async_fib(15));
    glist.push_back(co_async_fib(15,50));
    auto g = cocls::generator_aggregator(std::move(glist));
    while (co_await g.next()) {
        std::cout <<  g.value() << " " ;
    }
    std::cout<< std::endl;
}
cocls::task<void> co_multfib_reader2()  {
    std::cout<< "async gen multi - sync: ";
    std::vector<cocls::generator<int> > glist;
    glist.push_back(co_fib2(15));
    glist.push_back(co_fib2(15));
    auto g = cocls::generator_aggregator(std::move(glist));
    while (co_await g.next()) {
        std::cout <<  g.value() << " " ;
    }
    std::cout<< std::endl;
}


int test_mutex() {

    int shared_var = 0;
    std::default_random_engine rnd(0);
    cocls::mutex mx;
    cocls::thread_pool pool(4);
    std::vector<cocls::task<> > tasks;
    for (int i = 0; i < 20; i++) {
        auto t =([&](int &shr, cocls::mutex &mx, std::default_random_engine &rnd, int idx)->cocls::task<void>{
            co_await pool;
            std::cout << "Coroutine start:" << idx << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for (int i = 0; i < 5; i++) {
                auto own = co_await mx.lock();
                std::cout << "Coroutine running id: " << idx << std::endl;
                std::uniform_int_distribution<int> tm(0,100);
                auto x = ++shr;
                std::this_thread::sleep_for(std::chrono::milliseconds(tm(rnd)));
                assert(x == shr); //variable should not change here
                std::cout << "Shared var increased: " << shr << std::endl;

            }
        })(shared_var, mx, rnd, i);
        tasks.push_back(t);
    }
    for (auto &t: tasks) {
        t.join();
    }


    return shared_var;
    
    
}

void test_pause() {
    cocls::coroboard([]{
       for (int i = 0; i < 5; i++) {
           ([](int i)->cocls::task<void>{
              for (int j = 0; j < 5; j++) {
                  std::cout << "Running coroutine " << i << " cycle " << j << std::endl;
                  co_await cocls::pause();
              } 
              std::cout << "Finished coroutine " << i << std::endl;
           })(i);
       }     
    });
}


cocls::task<> threadpool_co(cocls::thread_pool &p) {
    std::cout<<"(threadpool_co) thread: " << std::this_thread::get_id() << std::endl;
    co_await p;
    std::cout<<"(threadpool_co) thread: " << std::this_thread::get_id() << std::endl;
    co_await cocls::thread_pool::current();
    std::cout<<"(threadpool_co) thread: " << std::this_thread::get_id() << std::endl;
    co_await p.fork([]{
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout<<"(threadpool_co) forked code: " << std::this_thread::get_id() << std::endl;
    });
    std::cout<<"(threadpool_co) thread: " << std::this_thread::get_id() << std::endl;
    co_await cocls::thread_pool::current::fork([]{
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout<<"(threadpool_co) forked code 2: " << std::this_thread::get_id() << std::endl;
    });
    std::cout<<"(threadpool_co) thread: " << std::this_thread::get_id() << std::endl;
}

void threadpool_test() {
    std::cout << "(threadpool_test) started" << std::endl;
    cocls::thread_pool pool(4);
    //sleep for while to allow all threads fully started
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    threadpool_co(pool).join();
    std::cout << "(threadpool_test) finished" << std::endl;
}

cocls::task<> scheduler_test_task(cocls::scheduler<> &sch) {
    std::cout << "(scheduler_test_task) started "<< std::endl;
    auto gen = sch.interval(std::chrono::milliseconds(100));    
    auto n = co_await gen;
    while (*n != 20) {        
        std::cout << "(scheduler_test_task) interval generator tick: " << (*n) << std::endl;
        n = co_await gen;        
    }
    std::cout << "(scheduler_test_task) exiting "<< std::endl;
    
}

void scheduler_test() {
    cocls::thread_pool pool(4);
    cocls::scheduler<> sch;
    sch.start(pool);
    scheduler_test_task(sch).join();
}

using queued_task = cocls::with_queue<cocls::task<void>, int>; 
 queued_task with_queue_task() {
        int i = co_await queued_task::current_queue();
        while (i) {
            std::cout<<"(with_queue_task) Received from queue: " << i << std::endl;
            i = co_await queued_task::current_queue();
        }
        std::cout<<"(with_queue_task) Done" << std::endl;        
}

void with_queue_test() {
    cocls::with_queue<cocls::task<void>, int> wq = with_queue_task();
    wq.push(1);
    wq.push(2);
    wq.push(3);
    wq.push(0);
    wq.join();    
}

cocls::no_alloc<cocls::task<void>,cocls::storage_t<> > test_reusable_co(cocls::storage_t<> &m, cocls::scheduler<> &sch) {
    std::cout << "(test_reusable_co) running" << std::endl;
    co_await sch.sleep_for(std::chrono::seconds(1));
    std::cout << "(test_reusable_co) finished" << std::endl;
    co_return;
}


void test_reusable() {
    cocls::storage_t<> m;
    cocls::thread_pool pool(1);
    cocls::scheduler<> sch(pool);
    //coroutine should allocate new block
    {
        test_reusable_co(m, sch).join();
    }
    //coroutine should reuse preallocated block
    {
        test_reusable_co(m, sch).join();
    }
    //coroutine should reuse preallocated block
    {
        test_reusable_co(m, sch).join();
    }
}

cocls::task<> subscriber_fast(cocls::publisher<int> &pub) {
    cocls::subscriber<int> src(pub);    
    for(;;) {
        std::optional<int> x = co_await src;
        if (!x.has_value()) break;
        std::cout<<"(subscriber_1) value ." << *x << std::endl;
    }
}

cocls::task<> subscriber_slow(cocls::publisher<int> &pub, cocls::scheduler<> &sch) {
    cocls::subscriber<int> src(pub);
    for(;;) {
        std::optional<int> x = co_await src;
        if (!x.has_value()) break;
        std::cout<<"(subscriber_2) value ..." << *x << std::endl;
        co_await sch.sleep_for(std::chrono::milliseconds(100));
    } 
}

cocls::task<> subscriber_slow2(cocls::publisher<int> &pub, cocls::scheduler<> &sch) {
    cocls::subscriber<int> src(pub, cocls::subscribtion_type::skip_to_recent);
    for(;;) {
        std::optional<int> x = co_await src;
        if (!x.has_value()) break;
        std::cout<<"(subscriber_3) value ..." << *x << std::endl;
        co_await sch.sleep_for(std::chrono::milliseconds(100));
    } 
}

void publisher_test() {
    cocls::publisher<int> pub;
    cocls::thread_pool thp(1);
    cocls::scheduler<> sch(thp);
    
    auto s1 = subscriber_fast(pub);
    auto s2 = subscriber_slow(pub, sch);;
    auto s3 = subscriber_slow2(pub, sch);;
    for (int i = 0; i < 10; i++) {
        pub.publish(i);
        if (i == 5) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    pub.close();
    s1.join();
    s2.join();
    s3.join();
    
}


int main(int argc, char **argv) {
    std::cout << "MIT License Copyright (c) 2022 Ondrej Novak" << std::endl;
    std::cout << "Version: " << GIT_PROJECT_VERSION << std::endl;
    std::cout << std::endl;
    std::cout << "(main) starting co_test2" << std::endl;    
    auto z = co_test2();
    std::cout << "(main) waiting for future" << std::endl;
    std::cout << (sync_await z) << std::endl;
 

    threadpool_test();
    
    scheduler_test();
    
    with_queue_test();
    
    test_reusable();

    publisher_test();
    
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
    while (fib3.next()) {
        std::cout << fib3.value() << " ";
    }
    std::cout<< std::endl;

    co_fib_reader().join();


    auto fib4 = co_async_fib(15);
    std::cout<< "async gen2 - next/read (sync): ";
    while (fib4.next()) {
        std::cout << fib4.value() << " ";
    }
    std::cout<< std::endl;

    co_fib2_reader().join();
    
    co_fib3_reader().join();
    
    co_multfib_reader().join();
    co_multfib_reader2().join();

    std::cout << "Mutex test" << std::endl;
    test_mutex();
    std::cout << "Pause test" << std::endl;
    test_pause();
    
}


