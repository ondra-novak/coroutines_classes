#include <iostream>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>

#include <mutex>

static std::mutex mx;

void ex_output(int id) {
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard _(mx);
        for (int k = 0; k < id; k++ ) {
            std::cout << "\t\t";
        }
        std::cout << std::this_thread::get_id() << std::endl;
    }
}

cocls::task<> co_test(cocls::thread_pool &pool) {
    co_await pool;
    ex_output(0);
    co_await pool.fork([]{
            ex_output(1);
    });
    ex_output(2);
}


int main(int, char **) {
    cocls::thread_pool pool(5);
    co_test(pool).join();
    
    

}
