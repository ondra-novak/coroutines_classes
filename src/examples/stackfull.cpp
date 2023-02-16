
#include <malloc.h>
#include <iostream>

#include <coclasses/resumption_policy.h>

template<>
struct cocls::resumption_policy::unspecified<void>{
    using policy = cocls::resumption_policy::immediate;
};


#include <coclasses/subtask.h>
#include <coclasses/generator.h>
#include <coclasses/thread_pool.h>
#include <coclasses/future.h>
#include <coclasses/stackfull.h>
#include <coclasses/dispatcher.h>



void *operator new(std::size_t sz) {
    void *ptr = malloc(sz);
    std::cout << "Alloc: " << sz << std::endl;
    return ptr;
}


void operator delete (void *ptr) noexcept {
    if (ptr) {
        auto sz = malloc_usable_size(ptr);
        std::cout << "Free:  " << sz << std::endl;
        free(ptr);
    }
}

cocls::stackfull<void> output_async(cocls::coro_stack &stack, double value) {
    co_await cocls::dispatcher::sleep_for(std::chrono::milliseconds(100));
    std::cout << value << std::endl;
}


cocls::stackfull<void> recursion(
            cocls::coro_stack &stack,
            double val,
            int cycle_id) {
    if (cycle_id == 1) co_await output_async(stack, val);
    else co_await recursion(stack, val * cycle_id, cycle_id-1);
}

cocls::stackfull<void> test(
            cocls::coro_stack &stack) {
    for (int i = 1; i < 20; i++) {
        co_await recursion(stack, 1, i);
    }
}




int main(int, char **) {
    cocls::dispatcher::init();
    cocls::coro_stack stack(10);
    cocls::dispatcher::await(test(stack));

}

