#include "../../version.h"
#include <coclasses/awaiter.h>
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
#include <coclasses/publisher.h>
#include <coclasses/queued_resumption_policy.h>
#include <coclasses/coro_storage.h>
#include <array>
#include <iostream>
#include <cassert>
#include <random>




cocls::lazy<int> co_lazy() {
    COCLS_SET_CORO_NAME();
    std::cout << "(co_lazy) executed" << std::endl;
    co_return 56;
}

cocls::task<int> co_test() {
    COCLS_SET_CORO_NAME();
    std::cout << "(co_test) started" << std::endl;
    cocls::future<int> f;
    auto cbp = cocls::make_promise<int>([](cocls::future<int> &x){
       std::cout << "(make_promise) called:" << x.value() << std::endl;
    });
    std::thread thr([p = f.get_promise(), p2 = std::move(cbp)]() mutable {
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
    COCLS_SET_CORO_NAME();
    std::cout << "(co_test2) co_lazy() started" << std::endl;
    cocls::lazy<int> lz = co_lazy();
    std::cout << "(co_test2) await" << std::endl;
    int i = co_await(co_test());
    int j = co_await lz;
    std::cout << "(co_test2) await finished i = " << i<< ", j = " << j << std::endl;
    co_return(i);
}

cocls::generator<int> co_fib() {
    COCLS_SET_CORO_NAME();
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
    COCLS_SET_CORO_NAME();
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
    COCLS_SET_CORO_NAME();
    int a = 0;
    int b = 1;
    for(int i = 0;i < count; i++) {
        cocls::future<int> cf;
        std::thread thr([&a,&b,&delay, cp = cf.get_promise()]() mutable {
            int c = a+b;
            a = b;
            b = c;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            cp.set_value(c);
        });
        thr.detach();
        int c = co_await cf;
        co_yield c;
    }
}

cocls::task<void> co_fib_reader()  {
    COCLS_SET_CORO_NAME();
    std::cout<< "async gen - co_await: "  << std::flush;
    auto g = co_async_fib(15);
    while (co_await g.next()) {
        std::cout << g.value() << " " ;
    }
    std::cout<< std::endl;
}
cocls::task<void> co_fib2_reader()  {
    COCLS_SET_CORO_NAME();
    std::cout<< "sync gen - co_await;"  << std::flush;
    auto g = co_fib2(15);
    while (co_await g.next()) {
        std::cout << g.value() << " " ;
    }
    std::cout<< std::endl;
}
cocls::task<void> co_fib3_reader()  {
    COCLS_SET_CORO_NAME();
    std::cout<< "sync gen - resume in coroutine: "  << std::flush;
    auto g = co_fib2(15);
    while (g.next()) {
        std::cout << g.value() << " " ;
    }
    std::cout<< std::endl;
    co_return; //need co_return to have this as coroutine
}
cocls::task<void> co_multfib_reader()  {
    COCLS_SET_CORO_NAME();
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
    COCLS_SET_CORO_NAME();
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
            COCLS_SET_CORO_NAME();
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
            std::cout << "Finished coroutine " << idx << std::endl;
        })(shared_var, mx, rnd, i);
        tasks.push_back(t);
    }
    for (auto &t: tasks) {
        t.join();
    }


    return shared_var;


}

void test_pause() {
    ([]()->cocls::task<>{
        COCLS_SET_CORO_NAME();
       for (int i = 0; i < 5; i++) {
           ([](int i)->cocls::task<void>{
              for (int j = 0; j < 5; j++) {
                  std::cout << "Running coroutine " << i << " cycle " << j << std::endl;
                  co_await cocls::pause<>();
              }
              std::cout << "Finished coroutine " << i << std::endl;
           })(i);
       }
       co_return;
    })().join();
}


cocls::task<> threadpool_co(cocls::thread_pool &p) {
    COCLS_SET_CORO_NAME();
    std::cout<<"(threadpool_co) thread: " << std::this_thread::get_id() << std::endl;
    co_await p;
    std::cout<<"(threadpool_co) thread: " << std::this_thread::get_id() << std::endl;
    co_await cocls::thread_pool::current();
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

cocls::task<> scheduler_test_task(cocls::scheduler &sch) {
    COCLS_SET_CORO_NAME();
    std::cout << "(scheduler_test_task) started "<< std::endl;
    auto gen = sch.interval(std::chrono::milliseconds(100));
    co_await gen.next();
    auto n = gen.value();
    while (n != 10) {
        std::cout << "(scheduler_test_task) interval generator tick: " << (n) << std::endl;
        co_await gen.next();
        n = gen.value();
    }
    std::cout << "(scheduler_test_task) exiting "<< std::endl;

}

void scheduler_test() {
    cocls::thread_pool pool(4);
    cocls::scheduler sch(pool);
    scheduler_test_task(sch).join();
}

using queued_task = cocls::with_queue<cocls::task<void>, int>;
 queued_task with_queue_task() {
     COCLS_SET_CORO_NAME();
        int i = co_yield {};
        while (i) {
            std::cout<<"(with_queue_task) Received from queue: " << i << std::endl;
            i = co_yield {};
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

cocls::with_allocator<cocls::reusable_storage, cocls::task<void> >
        test_reusable_co(cocls::reusable_storage &, cocls::scheduler &sch) {
    COCLS_SET_CORO_NAME();
    std::cout << "(test_reusable_co) running" << std::endl;
    co_await sch.sleep_for(std::chrono::seconds(1));
    std::cout << "(test_reusable_co) finished" << std::endl;
    co_return;
}


void test_reusable() {
    cocls::reusable_storage m;
    cocls::thread_pool pool(1);
    cocls::scheduler sch(pool);
    //coroutine should allocate new block
    {
        test_reusable_co(m,sch).join();
    }
    std::cout << "Coroutine allocated in storage. size=" << m.capacity() << std::endl;
    //coroutine should reuse preallocated block
    {
        test_reusable_co(m,sch).join();
    }
    //coroutine should reuse preallocated block
    {
        test_reusable_co(m,sch).join();
    }
}

cocls::task<> subscriber_fast(cocls::publisher<int> &pub) {
    COCLS_SET_CORO_NAME();
    cocls::subscriber<int> src(pub);
    while (co_await src.next()) {
        int x = src.value();
        std::cout<<"(subscriber_1) value ." << x << std::endl;
    }
}

cocls::task<> subscriber_slow(cocls::publisher<int> &pub, cocls::scheduler &sch) {
    COCLS_SET_CORO_NAME();
    cocls::subscriber<int> src(pub);
    while (co_await src.next()) {
        int x = src.value();
        std::cout<<"(subscriber_2) value ..." << x << std::endl;
        co_await sch.sleep_for(std::chrono::milliseconds(100));
    }
}

cocls::task<> subscriber_slow2(cocls::publisher<int> &pub, cocls::scheduler &sch) {
    COCLS_SET_CORO_NAME();
    cocls::subscriber<int> src(pub);
    while (co_await src.next()) {
        int x = src.value();
        std::cout<<"(subscriber_3) value ..." << x << std::endl;
        co_await sch.sleep_for(std::chrono::milliseconds(100));
        while (src.next_ready()); //skip all old values
    }
}

void publisher_test() {
    cocls::publisher<int> pub;
    cocls::thread_pool thp(1);
    cocls::scheduler sch(thp);

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
#ifdef COCLS_DEFINE_SET_CORO_NAME
void coro_monitor(std::atomic<bool> &exitflag) {
    cocls::debug_reporter::current_instance->coro_monitor_register();
    while (!exitflag) {
        auto coros = cocls::debug_reporter::current_instance->get_running_coros();
        std::cerr << "\033[2J\033[H";
        for (auto &[h, info]: coros) {
            std::cerr << "Running: " << h.address() << " - " << info.loc << ":" << info.fn << " " << info.name << std::endl;
        }
        std::cerr << "---------" << std::endl;
        cocls::debug_reporter::current_instance->coro_monitor_wait();
        cocls::debug_reporter::current_instance->coro_monitor_register();
    }
}
#endif


int main(int argc, char **argv) {
    std::cout << "MIT License Copyright (c) 2022 Ondrej Novak" << std::endl;
    std::cout << "Version: " << GIT_PROJECT_VERSION << std::endl;
    std::cout << std::endl;

#ifdef COCLS_DEFINE_SET_CORO_NAME
    std::atomic<bool> mon_exit = false;
    std::thread mon_thread([&]{coro_monitor(mon_exit);});
#endif
    {
        std::cout << "(main) starting co_test2" << std::endl;
        auto z = co_test2();
        std::cout << "(main) waiting for future" << std::endl;
        std::cout << z.join() << std::endl;
    }

    threadpool_test();

    scheduler_test();

    with_queue_test();

    test_reusable();

    publisher_test();

    {
        auto fib = co_fib();
        std::cout<< "infinite gen: " << std::flush;
        for (int i = 0; i < 15; i++) {
            auto iter = fib.begin();
            if (iter != fib.end()) {
                std::cout << *iter << " " ;
            }
        }
        std::cout<< std::endl;
    }

    {
        auto fib2 = co_fib2(15);
        std::cout<< "finite gen - range for: "  << std::flush;
        for (int &i: fib2) {
            std::cout << i << " ";
        }
        std::cout<< std::endl;
    }

    {
        auto fib3 = co_fib2(15);
        std::cout<< "finite gen - next/read: "  << std::flush;
        while (fib3.next()) {
            std::cout << fib3.value() << " ";
        }
        std::cout<< std::endl;
    }

    co_fib_reader().join();

    {
        auto fib4 = co_async_fib(15);
        std::cout<< "async gen2 - next/read (sync): "  << std::flush;
        while (fib4.next()) {
            std::cout << fib4.value() << " ";
        }
        std::cout<< std::endl;
    }

    co_fib2_reader().join();

    co_fib3_reader().join();

    co_multfib_reader().join();
    co_multfib_reader2().join();

    std::cout << "Mutex test" << std::endl;
    test_mutex();
    std::cout << "Pause test" << std::endl;
    test_pause();

#ifdef COCLS_DEFINE_SET_CORO_NAME
    mon_exit.store(true);
    cocls::coro_monitor_event();
    mon_thread.join();
#endif

}


