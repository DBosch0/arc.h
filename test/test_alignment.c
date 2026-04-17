/* test_alignment.c
 * Verifies that Rc, Arc, and ArcMutex headers are correctly sized and aligned
 * on both 64-bit and 32-bit builds (-m32 or native).
 *
 * The core requirement: sizeof(header) must be a multiple of max_align_t so
 * that user data following the header is correctly aligned for any type
 * (including double / int64_t which need 8-byte alignment on 32-bit).
 */

#define RC_IMPLEMENTATION
#define ARC_IMPLEMENTATION
#include "../rc.h"
#include "../arc.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* sizeof(header) must be a multiple of max_align_t */
_Static_assert(sizeof(Rc)    % _Alignof(max_align_t) == 0,
               "Rc header size is not a multiple of max_align_t");
_Static_assert(sizeof(Arc)   % _Alignof(max_align_t) == 0,
               "Arc header size is not a multiple of max_align_t");
_Static_assert(sizeof(ArcMutex) % _Alignof(max_align_t) == 0,
               "ArcMutex header size is not a multiple of max_align_t");

/* Platform-specific expected sizes */
#if SIZE_MAX == 0xFFFFFFFF          /* 32-bit */
_Static_assert(sizeof(size_t) == 4, "expected 32-bit size_t");
_Static_assert(sizeof(Rc)     == 16, "Rc should be 16 bytes on 32-bit");
_Static_assert(sizeof(Arc)    == 16, "Arc should be 16 bytes on 32-bit");
#else                               /* 64-bit */
_Static_assert(sizeof(size_t) == 8, "expected 64-bit size_t");
_Static_assert(sizeof(Rc)     == 32, "Rc should be 32 bytes on 64-bit");
_Static_assert(sizeof(Arc)    == 32, "Arc should be 32 bytes on 64-bit");
#endif

static void test_rc_struct_sizes(void) {
    printf("  sizeof(size_t)    = %zu\n", sizeof(size_t));
    printf("  max_align_t       = %zu\n", _Alignof(max_align_t));
    printf("  sizeof(Rc)        = %zu (multiple of max_align_t: %s)\n",
           sizeof(Rc),
           sizeof(Rc) % _Alignof(max_align_t) == 0 ? "yes" : "NO");
    printf("  sizeof(Arc)       = %zu (multiple of max_align_t: %s)\n",
           sizeof(Arc),
           sizeof(Arc) % _Alignof(max_align_t) == 0 ? "yes" : "NO");
    printf("  sizeof(ArcMutex)  = %zu (multiple of max_align_t: %s)\n",
           sizeof(ArcMutex),
           sizeof(ArcMutex) % _Alignof(max_align_t) == 0 ? "yes" : "NO");
    printf("[PASS] all header sizes are multiples of max_align_t\n");
}

#define DOUBLE_EPS 1e-10

static void test_rc_double_aligned(void) {
    /* Store a double inside an rc-managed block and verify it's aligned.
     * Exact equality is avoided because on 32-bit x86 the x87 FPU uses
     * 80-bit extended precision for register values; a round-trip through
     * memory truncates to 64-bit, so the stored value may differ from the
     * literal by a tiny rounding error.  An epsilon check is sufficient. */
    double *d = rc_alloc(sizeof(double), NULL);
    assert(d != NULL);
    assert((uintptr_t)d % _Alignof(double) == 0 && "double payload is misaligned");
    *d = 3.14159;
    assert(fabs(*d - 3.14159) < DOUBLE_EPS);
    rc_release(d);
    printf("[PASS] double payload is correctly aligned in Rc\n");
}

static void test_arc_double_aligned(void) {
    double *d = arc_alloc(sizeof(double), NULL);
    assert(d != NULL);
    assert((uintptr_t)d % _Alignof(double) == 0 && "double payload is misaligned");
    *d = 2.71828;
    assert(fabs(*d - 2.71828) < DOUBLE_EPS);
    arc_release(d);
    printf("[PASS] double payload is correctly aligned in Arc\n");
}

static void test_arc_mutex_double_aligned(void) {
    double *d = arc_mutex_alloc(sizeof(double), NULL);
    assert(d != NULL);
    assert((uintptr_t)d % _Alignof(double) == 0 && "double payload is misaligned");
    *d = 1.41421;
    assert(fabs(*d - 1.41421) < DOUBLE_EPS);
    arc_mutex_release(d);
    printf("[PASS] double payload is correctly aligned in ArcMutex\n");
}

static void test_int64_aligned(void) {
    int64_t *n = rc_alloc(sizeof(int64_t), NULL);
    assert(n != NULL);
    assert((uintptr_t)n % _Alignof(int64_t) == 0 && "int64_t payload is misaligned");
    *n = INT64_MAX;
    assert(*n == INT64_MAX);
    rc_release(n);

    int64_t *m = arc_alloc(sizeof(int64_t), NULL);
    assert(m != NULL);
    assert((uintptr_t)m % _Alignof(int64_t) == 0 && "int64_t payload is misaligned");
    *m = INT64_MIN;
    assert(*m == INT64_MIN);
    arc_release(m);

    printf("[PASS] int64_t payload is correctly aligned in Rc and Arc\n");
}

int main(void) {
    printf("=== alignment tests (sizeof(size_t) = %zu) ===\n\n",
           sizeof(size_t));
    test_rc_struct_sizes();
    printf("\n");
    test_rc_double_aligned();
    test_arc_double_aligned();
    test_arc_mutex_double_aligned();
    test_int64_aligned();
    printf("\nAll tests passed.\n");
    return 0;
}
