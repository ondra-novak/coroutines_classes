#include <iostream>
#include <coclasses/future.h>

int main(int, char **) {
    std::cout << "Create promise" << std::endl;
    {
        cocls::promise<int> p = cocls::make_promise<int>([](cocls::future<int> &f){
            std::cout << "Callback called: " << f.get() << std::endl;            
        });
        std::cout<<"Promise resolved." << std::endl;
        p.set_value(42);
        std::cout<<"Promise destroyed." << std::endl;   
    }    
    std::cout << "Finished"  <<std::endl;
    
    
}

