#include "../../version.h"
#include <coclasses/task.h>
#include <coclasses/future.h>
#include <coclasses/lazy.h>
#include <coclasses/generator.h>
#include <coclasses/mutex.h>
#include <coclasses/queue.h>
#include <coclasses/condition_variable.h>
#include <coclasses/thread_pool.h>
#include <coclasses/scheduler.h>
#include <coclasses/with_queue.h>
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
    co_return; //need co_return to have this as coroutine
}
cocls::task<void> co_multfib_reader()  {
    std::cout<< "async gen multi - co_await: ";
    std::vector<cocls::generator<int> > glist;
    glist.push_back(co_async_fib(15));
    glist.push_back(co_async_fib(15,50));
    auto g = cocls::generator_aggregator(std::move(glist));
    while (co_await g) {
        std::cout <<  g() << " " ;
    }
    std::cout<< std::endl;
}


int test_mutex() {

    int shared_var = 0;
    std::default_random_engine rnd(0);
    cocls::mutex mx;
    std::array<std::thread, 4> thrs;
    for (auto &t: thrs) {        
        int p = std::distance(thrs.data(), &t);
        t = std::thread([&,p]{           
            std::cout << "Thread start:" << p << std::endl;
            cocls::resume_lock::coboard([&]{
                for (int i = 0; i < 5; i++) {
                    //NOTE - lambda's closure disappear on first suspend
                    auto t =([](int &shr, cocls::mutex &mx, std::default_random_engine &rnd, int idx)->cocls::task<void>{
                        std::cout << "Coroutine start:" << idx << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        for (int i = 0; i < 5; i++) {                            
                            auto own = co_await mx;
                            std::cout << "Coroutine running id: " << idx << std::endl;
                            std::uniform_int_distribution<int> tm(0,100);
                            auto x = ++shr;
                            std::this_thread::sleep_for(std::chrono::milliseconds(tm(rnd)));
                            assert(x == shr); //variable should not change here
                            std::cout << "Shared var increased: " << shr << std::endl;
                            
                        }                    
                    })(shared_var, mx, rnd, p*10+i);
                }
            });
            std::cout << "Thread exit" << std::endl;
        });       
    }
    for (auto &t: thrs) {
        t.join();
    }
    return shared_var;
    
    
}

void test_pause() {
    cocls::coboard([]{
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

cocls::task<> test_cond_var() {
    std::mutex mx;
    std::unique_lock _(mx);
    cocls::condition_variable cond;
    bool flag = false;
    std::thread thr([&]{
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "(test_con_var) trigger flag = false" << std::endl;
        cond.notify_one();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        {
            std::unique_lock _(mx);
            std::cout << "(test_con_var) trigger flag = true" << std::endl;
            flag = true;
            cond.notify_one(_);
        }
    });
    thr.detach();
    std::cout << "(test_con_var) Coroutine waiting" << std::endl;
    co_await cond(_, [&]{return flag;});
    std::cout << "(test_con_var) Coroutine released" << std::endl;
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
    co_await gen;
    auto n = gen();
    while (n != 20) {
        std::cout << "(scheduler_test_task) interval generator tick: " << n << std::endl;
        co_await gen;
        n = gen();
    }
    std::cout << "(scheduler_test_task) exiting "<< std::endl;
    
}

void scheduler_test() {
    cocls::thread_pool pool(4);
    cocls::scheduler<> sch;
    sch.start(pool);
    scheduler_test_task(sch).join();
}

cocls::with_queue<cocls::task<void>, int> with_queue_task() {
        int i = co_await cocls::current_queue<cocls::task<void>, int>();
        while (i) {
            std::cout<<"(with_queue_task) Received from queue: " << i << std::endl;
            i = co_await cocls::current_queue<cocls::task<void>, int>();
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

int main(int argc, char **argv) {
    std::cout << "MIT License Copyright (c) 2022 Ondrej Novak" << std::endl;
    std::cout << "Version: " << GIT_PROJECT_VERSION << std::endl;
    std::cout << std::endl;
    std::cout << "(main) starting co_test2" << std::endl;    
    auto z = co_test2();
    std::cout << "(main) waiting for future" << std::endl;
    std::cout << z.join() << std::endl;

    test_cond_var().join();

    threadpool_test();
    
    scheduler_test();
    
    with_queue_test();
    
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
    
    co_multfib_reader().join();

    std::cout << "Mutex test" << std::endl;
    test_mutex();
    std::cout << "Pause test" << std::endl;
    test_pause();
    
}

template class cocls::scheduler<>;
