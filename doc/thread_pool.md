#Thread pool

```
#include <coclasses/thread_pool>
```

* The instance of `cocls::thread_pool` implements pool of threads.
* The constructor expect count of threads to create
* The destructor joins all created threads - which can block until last thread is finished
* A coroutine can `co_await` on thread pool which transfers the execution of the coroutine into the thread pool


## Transfer execution into thread pool

```
cocls::task<void> coroutine_example(cocls::thread_pool &pool) {
    co_await pool; 
    //now we are running in thread pool
}
```


## Fork

The function fork() allows to split code into two threads. Original coroutine is transfere to
different thread in the thread pool, while a code passed to the function `fork()` continues in this thread. This can be useful to implement a server, where the coroutine accepts connections and forked code processes the connection


```
cocls::task<void> server(cocls::thread_pool &pool, listenning_socket s) {
    co_await pool; 
    //now we are running in thread pool
    for(;;) {
        socket c = co_await s.accept();
        co_await pool.fork([c = std::move(c)]{
            work_with_client_connection(c);
        });    
    }
}
```

Above example transfer execition of coroutine into a new thread of the thread pool, while the
function `work_with_client_connection continues in the current thread

### BUG GCC 10.3+ - not fixed yet (2022-10-13)

Above code will crash on GCC 10.3 and above - bug has been reported. You need to use **clang** or rewrite code slighly

```
cocls::task<void> server(cocls::thread_pool &pool, listenning_socket s) {
    co_await pool; 
    //now we are running in thread pool
    for(;;) {
        socket c = co_await s.accept();
        auto forked_code = [c = std::move(c)]{
            work_with_client_connection(c);
        };
        co_await pool.fork(std::move(forked_code);
    }
}
```
