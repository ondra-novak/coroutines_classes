/**
 * @file callback_generator.cpp
 * 
 * Demonstration of using callback_generator.
 * The generator is recursive, the coroutine is collector.
 * 
 * The first example shows a generator expecting bool returing callback with
 * unlimited generation cycles, where coroutine must pass false to stop generation
 * 
 * The second example shows a generator expecting void returning callback with
 * limited generation cycles
 * 
 */
#include <coclasses/callback_generator.h>
#include <coclasses/task.h>
#include <iostream>

template<typename Fn>
void recursive_generator(Fn &&cb, std::size_t n=1, double r = 1) {
    if (cb(r)) {
        recursive_generator(std::forward<Fn>(cb), n+1, r*n);
    }    
}


cocls::task<> collector() {
    auto gen = cocls::make_callback_generator<bool(double)>([](auto &col, bool run){
        if (run) recursive_generator(col);
    });
    for (int i = 0; i < 10; i++) {
        std::optional<double> x = co_await gen(true);
        std::cout << *x << std::endl;
    }
    co_await gen(false);
}

template<typename Fn>
void recursive_generator2(Fn &&cb, std::size_t n=1, double r = 1) {
    if (n>0) {
        cb(r);
        recursive_generator2(std::forward<Fn>(cb), n-1, r*n);
    }    
}

cocls::task<> collector2() {
    auto gen = cocls::make_callback_generator<void(double)>([](auto &col){
        recursive_generator2(col,9);
    });
    std::optional<double> x = co_await gen();
    while (x.has_value()) {
        std::cout << *x << std::endl;
        x = co_await gen();
    }    
}



template class cocls::callback_generator<void,int,int(*)(cocls::collector<void,int>&, int)>;

int main(int, char **) {
    collector().join();
    collector2().join();
}
