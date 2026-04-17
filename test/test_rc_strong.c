#define _POSIX_C_SOURCE 200809L
#define RC_IMPLEMENTATION
#include "../rc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int destroy_called = 0;

static void destroy_int(void *data) { destroy_called++; (void)data; }
static void destroy_str(void *data) { free(*(char **)data); destroy_called++; }

static void test_basic_alloc_release(void) {
    destroy_called = 0;
    int *n = rc_alloc(sizeof(int), destroy_int);
    assert(n != NULL);
    *n = 42;
    rc_release(n);
    assert(destroy_called == 1);
    printf("[PASS] basic alloc + release\n");
}

static void test_acquire_keeps_alive(void) {
    destroy_called = 0;
    int *n     = rc_alloc(sizeof(int), destroy_int);
    int *alias = rc_acquire(n);
    rc_release(n);
    assert(destroy_called == 0 && "premature destroy");
    rc_release(alias);
    assert(destroy_called == 1);
    printf("[PASS] acquire keeps object alive\n");
}

static void test_multiple_owners(void) {
    destroy_called = 0;
#define N 5
    int *refs[N];
    refs[0] = rc_alloc(sizeof(int), destroy_int);
    assert(refs[0] != NULL);
    for (int i = 1; i < N; i++) refs[i] = rc_acquire(refs[0]);
    for (int i = 0; i < N - 1; i++) {
        rc_release(refs[i]);
        assert(destroy_called == 0);
    }
    rc_release(refs[N - 1]);
    assert(destroy_called == 1);
    printf("[PASS] %d owners, single destroy\n", N);
#undef N
}

static void test_nested_pointer(void) {
    destroy_called = 0;
    char **box = rc_alloc(sizeof(char *), destroy_str);
    assert(box != NULL);
    *box = strdup("hello, rc!");
    char **alias = rc_acquire(box);
    rc_release(box);
    assert(destroy_called == 0);
    rc_release(alias);
    assert(destroy_called == 1);
    printf("[PASS] nested heap pointer freed by destroy\n");
}

static void test_null_destroy(void) {
    int *n = rc_alloc(sizeof(int), NULL);
    assert(n != NULL);
    rc_release(n);
    printf("[PASS] null destroy is safe\n");
}

static void test_zero_size(void) {
    void *p = rc_alloc(0, NULL);
    assert(p != NULL);
    rc_release(p);
    printf("[PASS] zero-size alloc + release\n");
}

int main(void) {
    printf("=== rc strong-reference tests ===\n\n");
    test_basic_alloc_release();
    test_acquire_keeps_alive();
    test_multiple_owners();
    test_nested_pointer();
    test_null_destroy();
    test_zero_size();
    printf("\nAll tests passed.\n");
    return 0;
}
