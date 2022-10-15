#include <iostream>
#include <coclasses/task.h>
#include <coclasses/future.h>

//task returning void
cocls::task<int> cofn1() {
    cocls::future<int> fut;
    std::thread thr([p = fut.get_promise()]{
       p.set_value(42); 
    });
    thr.detach();
    co_return co_await fut;
}





int main(int, char **) {
    std::cout << "Result:" << cofn1().join() <<std::endl;
    
    
}

