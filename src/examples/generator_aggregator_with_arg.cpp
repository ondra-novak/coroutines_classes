#include <iostream>
#include <coclasses/generator.h>
#include <coclasses/generator_aggregator.h>
#include <coclasses/subtask.h>
#include <vector>

struct RetVal {
    int id;
    double sum = 0;
    int count = 0;
};


cocls::generator<RetVal(double)> summary(int id) {
    RetVal rv {id};
    for(;;) {
        double val = co_yield rv;
        rv.count++;
        rv.sum+=val;
    }
}

cocls::subtask<void> coro_example() {
    std::vector<cocls::generator<RetVal(double)> > gens;
    gens.push_back(summary(1)); 
    gens.push_back(summary(2));    
    auto sum = cocls::generator_aggregator(std::move(gens));
    double data[] = {1,4,32,31.3,58.3,0.2, 16.3, 0.8, 7.7,4,8.5};
    for (double x: data) {
        const RetVal *state = co_await sum(x);        
        std::cout << "Value=" << x <<", Id=" << state->id << ", Sum=" << state->sum << ",  Count=" << state->count << ", Avg=" << state->sum/state->count << std::endl;
    }
}


void sync_example() {
    std::vector<cocls::generator<RetVal(double)> > gens;
    gens.push_back(summary(1)); 
    gens.push_back(summary(2));    
    auto sum = cocls::generator_aggregator(std::move(gens));
    double data[] = {1,4,32,31.3,58.3,0.2, 16.3, 0.8, 7.7,4,8.5};
    for (double x: data) {
        const RetVal *state = sum(x);        
        std::cout << "Value=" << x <<", Sum=" << state->sum << ",  Count=" << state->count << ", Avg=" << state->sum/state->count << std::endl;
    }
}


int main() {
    
    coro_example().join();
    sync_example();
    return 0;
}


