# Exceptions

## await_canceled_exception

can appear when co_await operation has been canceled with no result or captured exception - so this exception is thrown instead

For example awaiting queue, or thread_pool, while these object are being destroyed. 


## value_not_ready_exception

is thrown from various places, where you can access the result without waiting (such a direct call await_resume) when the value is not ready yet, task is not finished and so on.

This exception can be thrown from co_await future when last promise is destroyed without setting a value

## no_more_values_exception

thrown from generator when attempt to retrieve value after generator finished



