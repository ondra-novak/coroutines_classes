#include <iostream>
#include <coclasses/task.h>

cocls::task<> test_task(int id) {
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < id; i++) std::cout << "\t";
        std::cout << j << std::endl;        
        co_await cocls::pause();
    }     
}


void test_cooperative() {
    cocls::coroboard([]{
       for (int i = 0; i < 5; i++) {
           test_task(i);
       }     
    });
}


int main(int, char **) {
    test_cooperative();
    
}
