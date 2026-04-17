# (Atomic) Reference Counting in C

Implements reference counting and atomic reference counting in two single header libraries `rc.h` and `arc.h`. The `rc.h` header was heavily inspired by [tsoding](https://www.youtube.com/@TsodingDaily)/[rexim](https://github.com/rexim/)'s stream on [Reference Counting in C](https://www.youtube.com/watch?v=iotrPxUnTdQ).

| Header | C standard | Thread-safe | Variants |
|--------|-----------|-------------|---------|
| `rc.h` | C11 | No | `Rc` (strong) + `RcWeak` (weak) |
| `arc.h` | C11 | Yes | `Arc` (lock-free) + `ArcMutex` (mutex-embedded) |

---

## Features

### rc.h
- **Strong references** — `rc_alloc` / `rc_acquire` / `rc_release`
- **Weak references** — `rc_downgrade` / `rc_weak_upgrade` / `rc_weak_release`
- **Optional destroy callback** invoked when the strong count reaches zero

### arc.h
Everything in `rc.h`, plus:
- **`Arc`** — lock-free atomic reference count using C11 `_Atomic`.  
  Data access is *not* protected; guard your payload separately (your own mutex,
  immutable data, or message-passing).
- **`ArcMutex`** — `pthread_mutex_t` embedded in the allocation header.  
  `arc_mutex_lock` / `arc_mutex_unlock` give exclusive access to the payload,
  and the reference count itself is also protected by the same mutex.
- **Weak references** for both `Arc` (`ArcWeak`) and `ArcMutex` (`ArcMutexWeak`)

---

## Requirements

| Library | C standard | Link flags |
|---------|-----------|------------|
| `rc.h`  | C11 or later (`-std=c11`) | — |
| `arc.h` | C11 or later (`-std=c11`) | `-lpthread` |

> **Why C11 for `rc.h`?**  The `Rc` header uses `_Alignas(max_align_t)` to guarantee that user data following the header is correctly aligned for any type (including `double` and `int64_t` on 32-bit platforms).

---

## Usage

Both libraries use the [stb-style](https://github.com/nothings/stb) single-header
convention: define the implementation macro in exactly one translation unit.

### rc.h

```c
// one .c file — provides the implementation
#define RC_IMPLEMENTATION
#include "rc.h"

// all other files — just include
#include "rc.h"
```

### arc.h

```c
// one .c file
#define ARC_IMPLEMENTATION
#include "arc.h"

// everywhere else
#include "arc.h"
```

---

## Examples

### Strong references (rc.h)

```c
#define RC_IMPLEMENTATION
#include "rc.h"
#include <stdio.h>

typedef struct { int x, y; } Point;

static void destroy_point(void *data) {
    printf("destroying point\n");
}

int main(void) {
    Point *p = rc_alloc(sizeof(Point), destroy_point);
    p->x = 1; p->y = 2;

    Point *alias = rc_acquire(p);   // count → 2

    rc_release(p);                  // count → 1  (no destroy yet)
    rc_release(alias);              // count → 0  → "destroying point"
}
```

### Weak references (rc.h)

Weak references are useful for breaking reference cycles — for example a child
that holds a back-reference to its parent without keeping the parent alive.

```c
Point *parent = rc_alloc(sizeof(Point), NULL);

RcWeak weak = rc_downgrade(parent); // weak ref; strong count stays 1

rc_release(parent);                 // parent destroyed; header kept alive by weak ref

Point *p = rc_weak_upgrade(&weak);  // NULL — parent is already gone
rc_weak_release(&weak);             // header freed here
```

### Thread-safe strong references (arc.h — Arc)

`Arc` is suitable for sharing immutable (or externally synchronised) data across
threads with minimal overhead.

```c
#define ARC_IMPLEMENTATION
#include "arc.h"

int *value = arc_alloc(sizeof(int), NULL);
*value = 42;

// Hand a new strong reference to another thread:
int *shared = arc_acquire(value);
// ... each thread calls arc_release(shared) when done
```

### Guarded mutable data (arc.h — ArcMutex)

`ArcMutex` embeds a mutex so the payload and the reference count are both
protected without any extra locking on the caller's side.

```c
int *counter = arc_mutex_alloc(sizeof(int), NULL);
*counter = 0;

// from any thread:
arc_mutex_lock(counter);
*counter += 1;
arc_mutex_unlock(counter);

arc_mutex_release(counter);
```

### Weak references (arc.h)

```c
int    *n = arc_alloc(sizeof(int), NULL);
ArcWeak w = arc_downgrade(n);           // weak ref; does not keep n alive

// from another thread, try to promote to a strong ref:
int *p = arc_weak_upgrade(&w);          // non-NULL only if n is still alive
if (p) {
    /* use p safely */
    arc_release(p);
}

arc_release(n);
arc_weak_release(&w);
```
