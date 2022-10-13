# Mutex

You can't use standard synchronzation primiteves in the coroutines, especially, when multiple threads are involved.

cocls::mutex introduces a mutex primitive, which can be used in coroutines. You need to co_await the mutex to obtain ownership. Awaiting mutex can cause suspenssion of the coroutine and when the mutex is unlocked, the awaiting coroutine is resumed

## ownership

**Ownership is object**

* You need to store the ownership in local variable of the coroutine. The mutex is unlocked after
you release the ownership (or automatically at its end of the lifetime)
* The ownership is **movable**, but **not copyable**

```
cocls::mutex mx

//obtain owneship
auto ownership = co_await mx.lock();

//release ownership
ownership.release()

```


## using Mutex in non-coroutine code

```
//obtain owneship
auto ownership = mx.lock().wait();

```


