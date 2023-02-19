#include <iostream>
#include <coclasses/generator.h>
#include <coclasses/subtask.h>

struct RetVal {
    double sum = 0;
    int count = 0;
};


cocls::generator<RetVal,double> summary() {
    //this is required - as we don't have first value.
    //by yielding nullptr as a constant, the current argument is fetched and returned
    //if you need to yield nullptr as value, you need to yield a variable containing nullptr
   //not doing this causes ignoring first argument by the generator
    double val = co_yield nullptr;
    RetVal rv;
    for(;;) {
        rv.count++;
        rv.sum+=val;
        val = co_yield rv;
    }
}

cocls::subtask<double> coro_example() {
    auto sum = summary();
    double data[] = {1,4,32,31.3,58.3,0.2, 16.3, 0.8, 7.7,4,8.5};
    for (double x: data) {
        const RetVal &state = co_await sum(x);
        std::cout << "Value=" << x <<", Sum=" << state.sum << ",  Count=" << state.count << ", Avg=" << state.sum/state.count << std::endl;
    }
    RetVal &st = sum.value();
    co_return st.sum/st.count;
}


double sync_example() {
    auto sum = summary();
    double data[] = {1,4,32,31.3,58.3,0.2, 16.3, 0.8, 7.7,4,8.5};
    for (double x: data) {
        const RetVal &state = *sum(x);
        std::cout << "Value=" << x <<", Sum=" << state.sum << ",  Count=" << state.count << ", Avg=" << state.sum/state.count << std::endl;
    }
    RetVal &st = sum.value();
    return st.sum/st.count;
}


int main() {

    double r1 = coro_example().join();
    std::cout << r1 << std::endl;
    double r2 = sync_example();
    std::cout << r2 << std::endl;

    return 0;
}


