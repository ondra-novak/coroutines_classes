# Future

Future is awaitable object. It can create a satellite `promise` object, which can resume awaiting coroutine and pass result to it

## cocls::future

### declaration

```
cocls::future<type> f;
```

Future object is neither copyable nor movable. If you need to move this object, use std::make_unique


### getting promise

```
auto p = f.get_promise();
```

Note, always avoid dangling promises, it is better to pass promise object directly to the asynchronous function

```
std::async([p = f.get_promise()]{
    //work with promise
});

```

### awaiting future

```
type result = co_await f
```

### awaiting future in non-coroutine code

```
type result = f.wait()
```

**Note** - there is no `wait_until`, `wait_for`

##  cocls::promise

* Promise is created by calling `future<X>::get_promise()`
* Promise is copyable and movable

### Setting value

* if `p` is `cocls::promise<int>`

```
p.set_value(42)
```

* if `p` is `cocls::promise<void>`

```
p.set_value()
```

### Capturing an exception and passing it to the coroutine

* if `p` is `cocls::promise<X>` 

```
try {
    ....
} catch (...) {
    p.unhandled_exception();
}
```

## When the coroutine is resumed

**NOTE** - the coroutine is not resumed immediately when a value is set or an exception is captureed. You need to destroy all promises copied or created by the future. Then, when last
instance of the promise is destroyed, the coroutine is resumed. This is the reason why you
need to avoid promises stored in local variables of the awaiting coroutine, because such
coroutine will never resumed

**wrong**

```
cocls::future<int> f;
auto p = f.get_promise();
do_async(p);
co_await f; //never resumes!
```

**correct**

```
cocls::future<int> f;
auto p = f.get_promise();
do_async(std::move(p));
co_await f; //never resumes!
```


## Specifying resume place manually (.release())

* If you have last promise instance in `p`. you can call `p.release()` to release promise  which causes resumption of awaiting coroutine at this point.
* Note if there are still existing copies, the function doesn't resume.

