#define _POSIX_C_SOURCE 200809L
#define ARC_IMPLEMENTATION
#include "../arc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int destroy_called = 0;
static void destroy_int(void *data) { destroy_called++; (void)data; }
static void destroy_str(void *data) { free(*(char **)data); destroy_called++; }

static void test_basic(void) {
    destroy_called = 0;
    int *n = arc_alloc(sizeof(int), destroy_int);
    assert(n != NULL);
    *n = 42;
    arc_release(n);
    assert(destroy_called == 1);
    printf("[PASS] basic alloc + release\n");
}

static void test_acquire_keeps_alive(void) {
    destroy_called = 0;
    int *n     = arc_alloc(sizeof(int), destroy_int);
    int *alias = arc_acquire(n);
    arc_release(n);
    assert(destroy_called == 0 && "premature destroy");
    arc_release(alias);
    assert(destroy_called == 1);
    printf("[PASS] acquire keeps object alive\n");
}

static void test_multiple_owners(void) {
    destroy_called = 0;
#define N 5
    int *refs[N];
    refs[0] = arc_alloc(sizeof(int), destroy_int);
    assert(refs[0] != NULL);
    for (int i = 1; i < N; i++) refs[i] = arc_acquire(refs[0]);
    for (int i = 0; i < N - 1; i++) {
        arc_release(refs[i]);
        assert(destroy_called == 0);
    }
    arc_release(refs[N - 1]);
    assert(destroy_called == 1);
    printf("[PASS] %d owners, single destroy\n", N);
#undef N
}

static void test_null_destroy(void) {
    void *p = arc_alloc(sizeof(int), NULL);
    assert(p != NULL);
    arc_release(p);
    printf("[PASS] null destroy is safe\n");
}

static void test_zero_size(void) {
    void *p = arc_alloc(0, NULL);
    assert(p != NULL);
    arc_release(p);
    printf("[PASS] zero-size alloc + release\n");
}

static void test_nested_pointer(void) {
    destroy_called = 0;
    char **box = arc_alloc(sizeof(char *), destroy_str);
    assert(box != NULL);
    *box = strdup("hello arc");
    char **alias = arc_acquire(box);
    arc_release(box);
    assert(destroy_called == 0);
    arc_release(alias);
    assert(destroy_called == 1);
    printf("[PASS] nested heap pointer freed by destroy\n");
}

int main(void) {
    printf("=== Arc strong-reference tests ===\n\n");
    test_basic();
    test_acquire_keeps_alive();
    test_multiple_owners();
    test_null_destroy();
    test_zero_size();
    test_nested_pointer();
    printf("\nAll tests passed.\n");
    return 0;
}
