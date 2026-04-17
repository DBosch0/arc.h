#define ARC_IMPLEMENTATION
#include "../arc.h"

#include <assert.h>
#include <stdio.h>

static int destroy_called = 0;
static void destroy_int(void *data) { destroy_called++; (void)data; }

static void test_upgrade_while_alive(void) {
    destroy_called = 0;
    int *n = arc_alloc(sizeof(int), destroy_int);
    assert(n != NULL);

    ArcWeak w  = arc_downgrade(n);
    int    *up = arc_weak_upgrade(&w);
    assert(up == n && "upgrade must succeed while strong ref is held");
    arc_release(up);                    /* count → 1 */
    arc_release(n);                     /* count → 0; destroy fires */
    assert(destroy_called == 1);

    assert(arc_weak_upgrade(&w) == NULL && "upgrade must fail after destroy");
    arc_weak_release(&w);
    assert(w.arc == NULL);
    printf("[PASS] upgrade succeeds while alive, fails after destroy\n");
}

static void test_header_stays_alive(void) {
    destroy_called = 0;
    int    *n  = arc_alloc(sizeof(int), destroy_int);
    ArcWeak w1 = arc_downgrade(n);
    ArcWeak w2 = arc_downgrade(n);

    arc_release(n);
    assert(destroy_called == 1);
    assert(arc_weak_upgrade(&w1) == NULL);
    arc_weak_release(&w1);
    arc_weak_release(&w2);              /* header freed here */
    printf("[PASS] header alive until last weak ref dropped\n");
}

int main(void) {
    printf("=== Arc weak-pointer tests ===\n\n");
    test_upgrade_while_alive();
    test_header_stays_alive();
    printf("\nAll tests passed.\n");
    return 0;
}
