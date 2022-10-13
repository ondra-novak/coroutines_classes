# Sync await

```
#include <coclasses/sync_await.h>


cocls::mutex mx;
///
auto ownership = sync_await mx.lock();
```

sync_await works similar as co_await, but it is intended to be used in non-coroutine code.
The awaiting cause blocking whole thread

**Note:** 
* `sync_await` is actually a macro, which is always global



