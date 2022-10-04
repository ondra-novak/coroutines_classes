## COCLASSES

Small template library with classes to support C++20 coroutines.

### namespace

The coclasses library is put inside `cocls` namespace.

You can put into your code

```
use namespace cocls;
```

### Types of supported tools?

* **task** 
* **lazy**
* **generator**
* **future**
* **mutex**
* **resume_lock/coboard**


### Task

Task represents coroutine, which can be use **co_await** and  **co_return**. Result of execution of
such coroutine is a future object `task<T>` which itself can be awaited. 

```
task<int> coroutine_example() {
    co_return 42; //very simple example
}

int main(int argc, char **argv) {
    task<int> result = coroutine_example();
    std::cout << result.get() << std::endl;

}
```

#### Object task<T> - features

* represents a coroutine itself and once the coroutine is finished, it can access to its result
* it acts as `shared_ptr` so it can be copied, which means, that reference to coroutine is shared
* can be moved
* can be awaited `co_await`, result of operation is return value of the coroutine
* if it is shared, multiple coroutines can await to single coroutine.
* outside of coroutine, you can check state of the coroutine using functions is_done()
* you can `get()` result of the coroutine, if the coroutine is not yet finished, it performs a blocking operation until the result is ready
* you can register callback function which is called when the coroutine is finished

### Lazy

* represents a lazily evaluated coroutine. It is executed once it is `co_await` for the first time. 
* the rest of features are similar to `task<T>`



### Generator

* represents a generator coroutine which can use co_yield to generate values. 
* can contain finite or infinite cycle. Once generator is no longer needed, it can be destroyed anytime
* to control generator and access generated values the object exposes an iterator. Working with the generator is similar to working with iterators

```
cocls::generator<int> co_fib(int count) {
    int a = 0;
    int b = 1;
    for(int i = 0;i < count; i++) {
        int c = a+b;        
        co_yield c;
        a = b;
        b = c;
    }
}

int main(int argc, char **argv) {
    auto fib = co_fib(15);
    for (int &i: fib) {
        std::cout << i << " ";
    }
    std::cout<< std::endl;
}

```

### Future

* represents awaitable object, which can hold future value. Along with the `future` object there is a satelite object `promise` which can be passed deep into code, and the promise is fulfilled, the `future` object is signaled and the result is available

```
cocls::future<int> fut;
run_async(fut.get_promise());  //run_async receives cocls::promise<int>                    
int result = co_await fut;
        //run_async(cocls::promise<int> p) {
        //  ...
        //  p.set_value(42);
        //}
```

**notes** 
* the `future` object can't be moved or copied
* the `promise` object can be copied or moved.
* You need to destroy all promises before the future object. Otherwise UO
* Only one `promise` can receive value. Receiving value is not MT Safe
* The awaiting coroutine is resumed only after all promises are destroyed. So it is not resumed immediately, this removes potential sideeffect in part of the code which is resolving the promise, and postopones resumption to the part when everything is probably destroyed.


### Mutex

* simple mutex which supports coroutines. 

```
cocls::mutex mx
...
//coroutine
{
    auto ownership = co_await mx;
    //you own mutex
    ownershop.reset()
    //you no longer own mutex
}

//standard function
{
    auto ownership = mx.lock();
    // you own mutex
    ownership.reset();
    //you no longer own mutex
```
Mutex works across threads and coroutine. Ownership of the mutex can be transfered between
threads. Ownership is automatically released at the end of function, if it is not transfered
(ownership is movable)

### Resume lock / coboard

Suspenssion and resumption of coroutines can build up the stack asi each operation creates a new frame. This issue is handled by resume_lock, which converts these operations to the symmetric transfer. This means resumption of coroutine don't need to be carried immediately, especially
in other coroutine. This operation can be schedules to next suspension point - so if you need
to ensure, that resumption is done immediately, you need to perform co_await on something

#### function pause()

Function `pause()` can be used to temporary suspend current coroutine in favor to other suspended coroutines that has been scheduled during `resume_lock`. 

```
co_await cocls::pause()
```

This function can be used to manually schedule coroutines.  Following code executes coroutines in parallel using manual scheduling

```
task<int> corun(int i) {
      for (int j = 0; j < 5; j++) {
          std::cout << "Running coroutine " << i << " cycle " << j << std::endl;
          co_await cocls::pause();
      } 
      std::cout << "Finished coroutine " << i << std::endl;
}

void interleaved() {
    cocls::coboard([]{
       for (int i = 0; i < 5; i++) {
            corun(i);
       }     
    });
}
```

#### function coboard()

Represents coroutine board, base level where resume lock is in effect. All coroutines suspended and resumed in current thread will use symetric transfer. Inside coboard you can start many coroutines as you want. Function exits when all created coroutines are finished or transfered
to different thread.


## Use in code

* you can include header files directly
* you can include library.cmake into your cmake project, and headers <coclasses/*> should become available

## Using promises in normal code

Promises can be used without need to introduce coroutines. You can easly create promise which executes callback once the promise is resolved


```
{
    cocls::promise<int> p = cocls::make_promise([=](future<int> &f) {
        int val = f.get();
        //work with value   
    });
    
    p.set_value(42);
    //...some other code
    
    //callback is called at the end of the block
}
