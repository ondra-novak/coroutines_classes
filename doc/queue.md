#Queue

Represents standard queue with ability to co_await on pop(). When the queue is empty, the coroutine is suspended and resumed after someone puts value into the queue.

```
cocls::queue<int> q;
q.push(42);



int val = co_await q.pop();
```

## features

* Queue is MT Safe. You can push values from multiple threads
* There can be multiple awaiters. They are kept and resumed in FIFO manner



## with_queue modifier for task<>

You can declare a task with_queue. It implements task's internal queue, The task can co_await on that queue to receive a data to process

```
cocls::with_queue<cocls::task<RValueT>, ItemT> coroutine(...) {

}
```

* **RValueT** return value of the task
* **ItemT** Type of item in the queue


### pushing value

if you have a such task in a variable `t`, you can call `t.push(<value>)` to push a value to the task's queue

```
cocls::with_queue<cocls::task<void>, int> coroutine_example() {

}

auto t = coroutine_example()

t.push(32)
```

### awaiting the internal queue

The coroutine with queue can await such a queue using declaring an instance of the awaiter `current_queue`

```
cocls::with_queue<cocls::task<void>, int> coroutine_example() {
    current_queue<cocls::task<void>, int> my_queue;
    for(;;) {
        int i = co_await my_queue;
        std::cout << i << std::endl;
    }
}
```

