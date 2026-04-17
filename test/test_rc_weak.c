#define RC_IMPLEMENTATION
#include "../rc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int destroy_called = 0;
static void destroy_int(void *data) { destroy_called++; (void)data; }

static void test_upgrade_while_alive(void) {
    destroy_called = 0;
    int *n = rc_alloc(sizeof(int), destroy_int);
    assert(n != NULL);
    *n = 7;

    RcWeak w      = rc_downgrade(n);
    int *upgraded = rc_weak_upgrade(&w);
    assert(upgraded == n && "upgrade must succeed while strong ref is held");
    assert(destroy_called == 0);

    rc_release(upgraded);               /* count → 1 */
    rc_release(n);                      /* count → 0; destroy fires */
    assert(destroy_called == 1);

    assert(rc_weak_upgrade(&w) == NULL && "upgrade must fail after destroy");
    rc_weak_release(&w);
    assert(w.rc == NULL);
    printf("[PASS] upgrade succeeds while alive, fails after destroy\n");
}

static void test_header_stays_alive(void) {
    destroy_called = 0;
    int    *n  = rc_alloc(sizeof(int), destroy_int);
    RcWeak  w1 = rc_downgrade(n);
    RcWeak  w2 = rc_downgrade(n);

    rc_release(n);                      /* strong → 0; header kept by weak refs */
    assert(destroy_called == 1);
    assert(rc_weak_upgrade(&w1) == NULL);

    rc_weak_release(&w1);               /* header still alive (w2 holds it) */
    rc_weak_release(&w2);               /* header freed here */
    assert(w1.rc == NULL && w2.rc == NULL);
    printf("[PASS] header alive until last weak ref released\n");
}

static void test_cycle_prevention(void) {
    /* Simulates parent→child (strong) and child→parent (weak).
     * Both objects must be destroyed without leaking the header. */
    destroy_called = 0;
    int *parent = rc_alloc(sizeof(int), destroy_int);
    int *child  = rc_alloc(sizeof(int), destroy_int);
    assert(parent && child);

    RcWeak back_ref = rc_downgrade(parent);

    rc_release(child);
    rc_release(parent);
    assert(destroy_called == 2 && "both objects must be destroyed");

    assert(rc_weak_upgrade(&back_ref) == NULL);
    rc_weak_release(&back_ref);
    printf("[PASS] cycle-like pattern; no leak\n");
}

int main(void) {
    printf("=== rc weak-pointer tests ===\n\n");
    test_upgrade_while_alive();
    test_header_stays_alive();
    test_cycle_prevention();
    printf("\nAll tests passed.\n");
    return 0;
}
