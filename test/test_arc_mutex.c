#define ARC_IMPLEMENTATION
#include "../arc.h"

#include <assert.h>
#include <stdio.h>

static int destroy_called = 0;
static void destroy_int(void *data) { destroy_called++; (void)data; }

/* ── strong reference tests ───────────────────────────────────────────────── */

static void test_basic(void) {
    destroy_called = 0;
    int *n = arc_mutex_alloc(sizeof(int), destroy_int);
    assert(n != NULL);
    *n = 99;
    arc_mutex_release(n);
    assert(destroy_called == 1);
    printf("[PASS] basic alloc + release\n");
}

static void test_acquire_keeps_alive(void) {
    destroy_called = 0;
    int *n     = arc_mutex_alloc(sizeof(int), destroy_int);
    int *alias = arc_mutex_acquire(n);
    arc_mutex_release(n);
    assert(destroy_called == 0 && "premature destroy");
    arc_mutex_release(alias);
    assert(destroy_called == 1);
    printf("[PASS] acquire keeps object alive\n");
}

static void test_multiple_owners(void) {
    destroy_called = 0;
#define N 5
    int *refs[N];
    refs[0] = arc_mutex_alloc(sizeof(int), destroy_int);
    assert(refs[0] != NULL);
    for (int i = 1; i < N; i++) refs[i] = arc_mutex_acquire(refs[0]);
    for (int i = 0; i < N - 1; i++) {
        arc_mutex_release(refs[i]);
        assert(destroy_called == 0);
    }
    arc_mutex_release(refs[N - 1]);
    assert(destroy_called == 1);
    printf("[PASS] %d owners, single destroy\n", N);
#undef N
}

static void test_null_destroy(void) {
    void *p = arc_mutex_alloc(sizeof(int), NULL);
    assert(p != NULL);
    arc_mutex_release(p);
    printf("[PASS] null destroy is safe\n");
}

static void test_lock_unlock(void) {
    int *n = arc_mutex_alloc(sizeof(int), NULL);
    assert(n != NULL);
    arc_mutex_lock(n);   *n = 123;   arc_mutex_unlock(n);
    arc_mutex_lock(n);   assert(*n == 123);   arc_mutex_unlock(n);
    arc_mutex_release(n);
    printf("[PASS] lock/unlock guards payload\n");
}

/* ── weak pointer tests ───────────────────────────────────────────────────── */

static void test_weak_upgrade_while_alive(void) {
    destroy_called = 0;
    int          *n  = arc_mutex_alloc(sizeof(int), destroy_int);
    ArcMutexWeak  w  = arc_mutex_downgrade(n);
    int          *up = arc_mutex_weak_upgrade(&w);
    assert(up == n);
    arc_mutex_release(up);
    arc_mutex_release(n);
    assert(destroy_called == 1);
    assert(arc_mutex_weak_upgrade(&w) == NULL);
    arc_mutex_weak_release(&w);
    assert(w.arc == NULL);
    printf("[PASS] weak: upgrade alive → ok, after destroy → NULL\n");
}

static void test_weak_header_stays_alive(void) {
    destroy_called = 0;
    int          *n = arc_mutex_alloc(sizeof(int), destroy_int);
    ArcMutexWeak  w = arc_mutex_downgrade(n);
    arc_mutex_release(n);
    assert(destroy_called == 1);
    assert(arc_mutex_weak_upgrade(&w) == NULL);
    arc_mutex_weak_release(&w);
    printf("[PASS] weak: header alive until weak ref dropped\n");
}

static void test_destroy_no_deadlock(void) {
    /* destroy callback runs without the mutex held — no deadlock possible */
    destroy_called = 0;
    int *n = arc_mutex_alloc(sizeof(int), destroy_int);
    assert(n != NULL);
    arc_mutex_release(n);
    assert(destroy_called == 1);
    printf("[PASS] destroy callback runs without deadlock\n");
}

int main(void) {
    printf("=== ArcMutex strong-reference tests ===\n\n");
    test_basic();
    test_acquire_keeps_alive();
    test_multiple_owners();
    test_null_destroy();
    test_lock_unlock();

    printf("\n=== ArcMutex weak-pointer tests ===\n\n");
    test_weak_upgrade_while_alive();
    test_weak_header_stays_alive();
    test_destroy_no_deadlock();

    printf("\nAll tests passed.\n");
    return 0;
}
