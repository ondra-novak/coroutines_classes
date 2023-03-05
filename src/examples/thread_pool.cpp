#include <iostream>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>



cocls::task<int> co_test(cocls::thread_pool &pool) {

    auto example_fn = []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return 42;
    };


    std::cout << "cur thread " << std::this_thread::get_id() << std::endl;
    co_await pool;
    std::cout << "cur thread " << std::this_thread::get_id() << std::endl;
    int r = co_await pool.run(example_fn);
    std::cout << "cur thread " << std::this_thread::get_id() << std::endl;
    co_return r;
}


int main(int, char **) {
    cocls::thread_pool pool(5);
    int r = co_test(pool).join();
    std::cout << r << std::endl;



}
