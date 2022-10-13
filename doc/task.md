# Task

## declaration

```
cocls::task<type> coroutine(...) {
    co_await ...;
    co_return ...;

}

```

## awaiting task

You can co_await the task

```
type x =co_await coroutine(...)
```

## joining task

It is similar to a thread, you can join the coroutine. Join is blocking operation

```
type x = coroutine(...).join()
```


## Task is movable and copyable

```
cocls::task<type> t1 = coroutine(...);
cocls::task<type> t2 = t1
```

## It is possible to co_await / join the single task on multiple places

There is only limitation - it is not allowed to multiple co_await one task instance. Always
create a copy and await that copy

## Dropping  result

It is possible to drop result before the task finishes. The task continues to run and it is destroyed on its exit

## Exceptions

Exceptions are propagated to the awaiting coroutines/threads


```
try {
    type x =co_await coroutine(...)
} catch (const std::exception &e) {
    //handle exception here
}
```

