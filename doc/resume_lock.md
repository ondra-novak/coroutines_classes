# Resume Lock

* The Resume Lock is feature which prevents resume-hell

## what is resume-hell

* Resume hell is a hell which is result of calling resume of coroutines without additional cooperation

```
  + -a.resume()
     |
     + - b.resume()
         |
         + - c.reusme()
             |
             + - d.reusme()
                 |
                 + - a.reusme() - crash
```

## what Resume Lock solves

* Resume Lock - The first resume holds a lock which prevents recursive resumes
* Resume Lock manages **thread_local** queue of resume requests. When a particula coroutine is being suspended, the *Resume Lock* pick first coroutine from the resume queue and by using feature **symmetric transfer** replaces suspending coroutine with coroutine being resumed

```
  + -a.resume()
     |
     + - b.resume() -> put in queue
     |
     a co_await
     b resumes
     |
     + - c.resume() -> put in queue
     |
     b co_await
     c resumes
     |
     + - d.resume() -> put in queue
     |
     c co_await
     d resumes
     |
     + - a.resume() -> put in queue
     |
     d co_await
 + - a contines
 
```
## using Resume Lock in coroutines

No action is needed, this is handled automatically

## Rules to use Resume Lock while resuming coroutines manually

* don't use `h.resume()` (where `h` is `std::coroutine_handle<>`) - use `cocls::resume_lock::resume(h)`
* if you writting awaiter, always use  `std::coroutine_handle<> await_suspend(std::coroutine_handle<>)` prototype. Then you need to call `cocls::resume_lock::await_suspend()` to retrieve return value for this function, if your awaiter wants to suspend current coroutine


Example awaiter

```
class example_awaiter {
public:
    bool await_ready() {return false;}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        _suspended = h;
        //function returns coroutine which replaces currently suspending coroutine
        return cocls::resume_lock::await_suspend();
    }
    void await_resume() {}
    void resume() {
        //can put resuming coroutine to the queue
        cocls::resume_lock::resume(_suspended);
    }
    
protected:
    std::coroutine_handle _suspended;
}
```


## coroboard()

Coroboard is function which acquires Resume Lock and executes a function which has been
passed as argument. Main purpose of the function is to create a frame to run many coroutines in the current thread, where it is excepted, that coroutines will suspend and resume each other.

```
cocls::coroboard([&]{
    run_coro1();
    run_coro2();
    run_coro3();
    ...//etc
})
```
the function exits, when queue is empty, so all coroutines finished, or their execution context was transfered to a different thread

The coroboard is automatically active in thread_pool's thread

