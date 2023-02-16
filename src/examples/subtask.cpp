#include <coclasses/subtask.h>
#include <coclasses/generator.h>
#include <coclasses/thread_pool.h>
#include <coclasses/future.h>

#include <iostream>

cocls::generator<int> co_fib(cocls::thread_pool &pool) {
    int a = 0;
    int b = 1;
    for(;;) {
        int c = a+b;
        co_yield c;
        co_await pool;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        a = b;
        b = c;
    }
}

cocls::subtask<int> reader(cocls::generator<int> gen) {
    int sum = 0;
    for (int i = 0; i < 20; i++) {
        bool has_next = co_await gen.next();
        if (has_next) {
            auto val = gen.value();
            sum+= val;
            std::cout << val << std::endl;
        } else {
            std::cout << "Done" << std::endl;
            break;
        }
    }
    co_return sum;

}

cocls::subtask<void> reader_task(cocls::generator<int> gen, int &res) {
    res = co_await reader(std::move(gen));
}

int main(int, char **) {

    cocls::thread_pool pool(4);
    int res;
    reader_task(co_fib(pool), res).join();
    std::cout << "Sum:" << res << std::endl;

}

