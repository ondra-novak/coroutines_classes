#include <iostream>
#include <coclasses/task.h>
#include <coclasses/thread_pool.h>


cocls::task<> co_test(cocls::thread_pool &pool) {
    std::cout << "cur thread " << std::this_thread::get_id() << std::endl;
    co_await pool;
    std::cout << "cur thread " << std::this_thread::get_id() << std::endl;
}


int main(int, char **) {
    cocls::thread_pool pool(5);
    co_test(pool).join();
    
    

}
