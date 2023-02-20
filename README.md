# COCLASSES

Small template library with classes to support C++20 coroutines.

## namespace

The coclasses library is put inside `cocls` namespace.

You can put into your code

```
use namespace cocls;
```

## Types of supported tools?

* **task\<T>** 
* **lazy\<T>**
* **generator\<T>**
* **future\<T>**
* **mutex**
* **queue\<T>**
* **thread_pool**
* **scheduler**
* **publisher\<T>** - **subscriber\<T>**
* **resume_lock framework**
* **coroboard/pause/manual scheduling**
* **sync_await**
* **abstract_awaiter**

## Explore examples

* [Examples](src/examples) 


## task\<T>

* awaitable (you can `co_await`)
* joinable (function `.join()` )
* copyable - sharing
* movable
* multi-await supported
* task\<void>, task<> 
* captures exceptions

## lazy\<T>

* task which starts suspended and it is resumed  on the first `co_await`

## generator\<T>

* finite or infinte
* awaitable (you can co_await)
* iterator (range for for finite generator)
* async supported - generator can co_await
* destroyable - you can destroy generator when it is suspended
* you can request to call a callback when generator suspends on `co_yield`

## future\<T>

* awaitable object (you can `co_await`)
* allocated on stack/frame (no memory allocation)
* future\<T> is not copyable nor movable
* satellite object `promise\<T>` can be retrieved by `.get_promise()`
* `promise\<T>` is copyable and movable
* use `promise\<T>` to set value of the future
* don't forget to destroy all instances of `promise\<T>` to resume awaiting task
* can be synchronously waited (function `.wait()`)
* you can create callback promise (function `.make_promise()`)

## mutex

* awaitable object
* ownership is tracked by object `mutex::ownership` which is movable
* lock() and try_lock()
* mutex is not tied with thread, so you can hold the ownership even if the coroutine is resumed in a different thread
* no memory allocation
* lockfree

## queue\<T>

* awaitable (`co_await queue.pop()`)
* watiable (`queue.pop().wait()`)
* multiple awaiter supported
* multiple pushers supported
* MT Safe

## thread_pool

* awaitable - (you can `co_await`)
* awaiting thread pool causes to transfer execution into the thread pool
* operation fork() - split execution to two threads

## scheduler<>

* awaitable `.sleep_for()`, `.sleep_until()`
* awaitable - `pause()` - suspends current coroutine in favor to other ready coroutines
* bind to a thread_pool - allocates one thread
* single-threaded mode - it is running, when there is no coroutine to execute
* custom clock, and clock's traits

## publisher\<T> - subscriber\<T>

* publisher - function publish
* subscriber - awaitable object -> returns `std::optional\<T>`
* easy to use
* shared queue, subscribers can read unprocessed data after the publisher is destroyed.

## resume_lock framework

* starts when a coroutine resumes other coroutine to prevent resume recursion
* in this case, coroutines are not resumed immediately. Resumption is done on next
`co_await`
* resume_lock framework is thread local feature
* if you write your own awaiter you should use following functions
    * instead `h.resume()` use `cocls::resume_lock::resume(h)`
    * for `await_suspend`, always return result of `cocls::resume_lock::await_suspend()`
    * this automatically uses *symmetric transfer*

## coroboard/pause/manual scheduling

* coroboard - framework (coroutine board) for manual scheduling - cooperative multitasking
* coroboard() function - enters to manual scheduling and calls a specified function

```
cocls::coroboard([&]{
    //execute under coroboard()
});
```

* all coroutines under coroboard are scheduled using *symmetric transfer*
* no coroutine is resumed immediately, it is always resumed on suspend of an other coroutine (use co_await or co_yield)
* function **pause** - allows you co use co_await without waiting on anything, however this allows to resume other prepared coroutines

```
co_await cocls::pause();
```

* this allows cooperative multitasking. One coroutne yields running in favor of another coroutine. You cannot choose which coroutine will be resumed. 
    * if you need to resume exact coroutine, you need to have its handle. Then you can call `cocls::resume_lock::resume(handle)`. Note that this doesn't cause resumption, but choosen coroutine will be scheduled and resumed on `co_await pause()`
    * if you want to get handle of current coroutine, you need to suspsnd it first. For this purpose there is function cocls::suspend(), which calls a custom callback and pass coroutine handle after the coroutine is suspended

## sync_await

* `sync_await` is macro
* works similar as `co_await` but can be used in non-coroutine function
* suspends whole thread
* use only, if the object doesn't support other ways to wait - for `task<>` it is `.join()`, other objects are probably have `wait()`
* the most of awaiters support `wait()`



```
scheduler<> sch;

(sch.sleep_for(x).wait()
```


## abstract_awaiter

* abstract_awaiter is base class for awaiters in `cocls`. It has virtual function `resume()` which is called for resumption of coroutine
* you can write own implementation, so you can receive signal instead resumption
* you can call `subscribe_awaiter(abstract_awaiter)` on a co_awaiter<> (most of awaiters in this library), so instead of waiting in coroutine or synchronously, your awaiter will be called for `.resume()` when awaitable object is ready

### co_awaiter\<X,chain>

* co_awaiter is awaitable object. The most primitives of this library uses this awaiter to implement co_await. It also supports
    * `.wait() - synchronous waiting
    * `.subscribe_awaiter() - subscribe custom awaiter
    

## Use in code

* you can include header files directly
* you can include library.cmake into your cmake project, and headers <coclasses/*> should become available

## Reference manual

* use **Doxygen** 
