#define ARC_IMPLEMENTATION
#include "../arc.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define NTHREADS 8

static _Atomic int destroy_called;
static void destroy_int(void *data) { atomic_fetch_add(&destroy_called, 1); (void)data; }

/* ── Arc: concurrent acquire/release ─────────────────────────────────────── */

static void *thread_acquire_release(void *arg) {
    void *data = arg;
    arc_acquire(data);
    arc_release(data);
    return NULL;
}

static void test_arc_concurrent_acquire_release(void) {
    atomic_store(&destroy_called, 0);
    int *n = arc_alloc(sizeof(int), destroy_int);
    assert(n != NULL);

    pthread_t threads[NTHREADS];
    for (int i = 0; i < NTHREADS; i++)
        pthread_create(&threads[i], NULL, thread_acquire_release, n);
    for (int i = 0; i < NTHREADS; i++)
        pthread_join(threads[i], NULL);

    assert(atomic_load(&destroy_called) == 0 && "main still holds a ref");
    arc_release(n);
    assert(atomic_load(&destroy_called) == 1);
    printf("[PASS] arc: %d threads concurrent acquire/release, single destroy\n", NTHREADS);
}

/* ── Arc: concurrent weak upgrade race ───────────────────────────────────── */

typedef struct { ArcWeak *weak; _Atomic int success; } WeakRaceArg;

static void *thread_weak_upgrade(void *arg) {
    WeakRaceArg *wra = arg;
    int *p = arc_weak_upgrade(wra->weak);
    if (p) { atomic_fetch_add(&wra->success, 1); arc_release(p); }
    return NULL;
}

static void test_arc_concurrent_weak_upgrade(void) {
    atomic_store(&destroy_called, 0);
    int    *n = arc_alloc(sizeof(int), destroy_int);
    ArcWeak w = arc_downgrade(n);
    WeakRaceArg arg = { &w, 0 };

#define RACE 16
    pthread_t threads[RACE];
    for (int i = 0; i < RACE; i++)
        pthread_create(&threads[i], NULL, thread_weak_upgrade, &arg);
    for (int i = 0; i < RACE; i++)
        pthread_join(threads[i], NULL);
#undef RACE

    assert(atomic_load(&arg.success) == 16 && "all upgrades must succeed");
    assert(atomic_load(&destroy_called) == 0 && "main still holds a ref");

    arc_release(n);
    arc_weak_release(&w);
    assert(atomic_load(&destroy_called) == 1);
    printf("[PASS] arc: 16 threads concurrent weak upgrade — all succeed\n");
}

/* ── ArcMutex: concurrent guarded writes ─────────────────────────────────── */

static void *thread_mutex_increment(void *arg) {
    int *counter = arg;
    arc_mutex_lock(counter);
    *counter += 1;
    arc_mutex_unlock(counter);
    return NULL;
}

static void test_arc_mutex_concurrent_writes(void) {
    int *counter = arc_mutex_alloc(sizeof(int), NULL);
    assert(counter != NULL);
    *counter = 0;

    pthread_t threads[NTHREADS];
    for (int i = 0; i < NTHREADS; i++)
        pthread_create(&threads[i], NULL, thread_mutex_increment, counter);
    for (int i = 0; i < NTHREADS; i++)
        pthread_join(threads[i], NULL);

    assert(*counter == NTHREADS && "data race would produce wrong count");
    arc_mutex_release(counter);
    printf("[PASS] arc_mutex: %d threads increment under lock → %d\n", NTHREADS, NTHREADS);
}

/* ── ArcMutex: concurrent acquire/release ────────────────────────────────── */

static void *thread_mutex_acquire_release(void *arg) {
    void *data = arg;
    arc_mutex_acquire(data);
    arc_mutex_release(data);
    return NULL;
}

static void test_arc_mutex_concurrent_acquire_release(void) {
    atomic_store(&destroy_called, 0);
    int *n = arc_mutex_alloc(sizeof(int), destroy_int);
    assert(n != NULL);

    pthread_t threads[NTHREADS];
    for (int i = 0; i < NTHREADS; i++)
        pthread_create(&threads[i], NULL, thread_mutex_acquire_release, n);
    for (int i = 0; i < NTHREADS; i++)
        pthread_join(threads[i], NULL);

    assert(atomic_load(&destroy_called) == 0);
    arc_mutex_release(n);
    assert(atomic_load(&destroy_called) == 1);
    printf("[PASS] arc_mutex: %d threads concurrent acquire/release, single destroy\n", NTHREADS);
}

int main(void) {
    printf("=== Arc threaded tests ===\n\n");
    test_arc_concurrent_acquire_release();
    test_arc_concurrent_weak_upgrade();

    printf("\n=== ArcMutex threaded tests ===\n\n");
    test_arc_mutex_concurrent_writes();
    test_arc_mutex_concurrent_acquire_release();

    printf("\nAll tests passed.\n");
    return 0;
}
