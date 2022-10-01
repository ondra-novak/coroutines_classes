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

#### Object lazy<T> - features

* represents a lazily evaluated coroutine. It is executed once it is `co_await` for the first time. 
* the rest of features are similar to `task<T>`

#### Object generator<T> - features

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

#### Object future

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


## Use in code

* you can include header files directly
* you can include library.cmake into your cmake project, and headers <coclasses/*> should become available

