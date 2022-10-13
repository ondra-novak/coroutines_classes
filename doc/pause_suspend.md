# Pause

Function `pause()` pauses current coroutine and resumes other waiting coroutine - if there is any

```
co_await pause()
```

If there is no such coroutine, it does nothing.


The pause can be used in thread_pool to postpone execution of a coroutine

```
std::task<> coro2() {
    co_await cocls::pause();
    //postponed - now we know, that creator finished its executoin    
    do_other_work();

std::task<> coro1(thread_pool &p) {
    co_await p;
    //transfer execution to the thread pool
    coro2(); //start execution of coro2(), but it will be postponed
    do_other_work();
    //coro1 finishes now
    //coro2 continues here

}
```

Function `cocls::pause()` need active `Resume Lock` or `coroboard()` or must be inside of `thread_pool`, otherwise it does nothing


# Suspend

The function `suspend()` suspends current coroutine and exposes its handle. You can then pass the handle whenever you want. To resume this coroutine you need to call `.resume()` or better `cocls::resume_lock::resume()`

```
co_await suspend([](std::coroutine_handle h) {
    //now we have handle of itself
    work_with_handle(h)
    //eventually
    cocls::resume_lock::resume(h);
}
```

