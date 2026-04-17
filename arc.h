/* arc.h — Single-header atomic reference counting for C (C11)
 * ============================================================
 *
 * Requires: -std=c11  (_Atomic, _Alignas, <stdatomic.h>)
 *           -lpthread (ArcMutex variant)
 *
 * Thread-safe counterpart to rc.h.  Two variants are provided:
 *
 *   Arc       — lock-free atomic reference count.  The data payload is NOT
 *               protected; guard it separately (your own lock, immutable data,
 *               or message-passing).  Equivalent to Rust's Arc<T>.
 *
 *   ArcMutex  — mutex embedded in the header.  Protects both the reference
 *               count and the data payload (via arc_mutex_lock/unlock).
 *               Equivalent to Rust's Arc<Mutex<T>>.
 *
 * STRONG REFERENCES
 *   arc_alloc / arc_acquire / arc_release
 *   arc_mutex_alloc / arc_mutex_acquire / arc_mutex_release
 *
 * WEAK REFERENCES
 *   arc_downgrade / arc_weak_upgrade / arc_weak_release
 *   arc_mutex_downgrade / arc_mutex_weak_upgrade / arc_mutex_weak_release
 *
 * DATA ACCESS (ArcMutex only)
 *   arc_mutex_lock / arc_mutex_unlock
 *
 * CUSTOM ALLOCATOR
 *   Define both macros before inlcuding this header:
 *
 *     #define ARC_MALLOC(sz)      my_malloc(sz)
 *     #define ARC_FREE(p)         my_free(p)
 *     #include "arc.h"
 *
 *   Defining only one is an error.  When none are defined the
 *   library falls back to malloc / free from <stdlib.h>.
 *
 * USAGE
 *   In exactly one translation unit:
 *     #define ARC_IMPLEMENTATION
 *     #include "arc.h"
 *   Everywhere else:
 *     #include "arc.h"
 *
 * MEMORY ORDERING (Arc)
 *   acquire : relaxed  — atomicity only; pointer transfer already synced.
 *   release : acq_rel  — release: our stores propagate before count drops;
 *                        acquire: final-decrement thread sees all prior
 *                        stores before calling destroy.
 *   upgrade : acquire on successful CAS — synchronise with prior stores.
 *
 * STRUCT SIZES
 *   Arc        32 bytes on 64-bit, 16 bytes on 32-bit  (max_align_t-aligned user data)
 *   ArcMutex   64 bytes on 64-bit, 48 bytes on 32-bit  (_Alignas(max_align_t) padded)
 */

#ifndef ARC_H_
#define ARC_H_

#include <stddef.h>
#include <pthread.h>

/* ── Allocator macros ────────────────────────────────────────────────────────
 * Must define both or none; partial definition is a compile error.
 */
#if defined(ARC_MALLOC) && defined(ARC_FREE)
    /* all provided — ok */
#elif !defined(ARC_MALLOC) && !defined(ARC_FREE)
    /* none provided — defaults applied below */
#else
#   error "Must define both or none of RC_MALLOC, and RC_FREE."
#endif

#ifndef ARC_MALLOC
#   define ARC_MALLOC(sz)     malloc(sz)
#   define ARC_FREE(p)        free(p)
#endif

/* ── Arc ─────────────────────────────────────────────────────────────────────
 * Lock-free atomic refcount.
 *
 * _Alignas(max_align_t) on the first field forces sizeof(Arc) to be a multiple
 * of max_align_t so that user data following the header is correctly aligned
 * on all platforms, including 32-bit (where 3×4 = 12 would misalign doubles).
 *   64-bit: sizeof == 32   32-bit: sizeof == 16
 */
typedef struct {
    _Alignas(max_align_t) _Atomic size_t  count;       /* strong reference count */
    _Atomic size_t  weak_count;  /* weak refs + 1 phantom              */
    void           (*destroy)(void *data);
} Arc;

/* ArcWeak — non-owning Arc reference.  arc field is NULL after release. */
typedef struct { Arc *arc; } ArcWeak;

/* arc_alloc — allocate a new Arc-managed object.
 *   size    : byte size of user data (may be 0).
 *   destroy : optional callback at strong-count zero; receives user data ptr.
 * Returns user data pointer, or NULL on failure.  Initial strong count == 1.
 */
void *arc_alloc(size_t size, void (*destroy)(void *data));

/* arc_acquire — increment strong count.  data must have a live reference. */
void *arc_acquire(void *data);

/* arc_release — decrement strong count; calls destroy and frees at zero. */
void  arc_release(void *data);

/* arc_downgrade — create an ArcWeak from a live strong reference. */
ArcWeak arc_downgrade(void *data);

/* arc_weak_upgrade — try to get a strong reference.
 * Returns user data pointer (strong count incremented) or NULL if destroyed.
 * Uses a CAS loop; safe to call concurrently with arc_release from any thread.
 */
void *arc_weak_upgrade(ArcWeak *weak);

/* arc_weak_release — drop a weak reference; frees header when last ref gone. */
void  arc_weak_release(ArcWeak *weak);


/* ── ArcMutex ────────────────────────────────────────────────────────────────
 * Mutex-embedded refcount.  sizeof(ArcMutex) == 64 on x86-64 Linux.
 *
 * _Alignas(max_align_t) on the mutex field forces sizeof to a multiple of 16
 * so that user data at (ArcMutex*)header + 1 is correctly aligned.
 *
 * weak_count is _Atomic (not mutex-protected) so that arc_mutex_weak_release
 * can safely decrement it after the mutex has been destroyed.
 *
 * destroy is called WITHOUT the mutex held to prevent deadlock if the
 * callback needs to take the same lock.  At that point count == 0, so no
 * new acquires are possible.
 */
typedef struct {
    _Alignas(max_align_t) pthread_mutex_t mutex;
    size_t          count;       /* strong count; protected by mutex    */
    _Atomic size_t  weak_count;  /* weak refs + 1 phantom; atomic       */
    void           (*destroy)(void *data);
} ArcMutex;

/* ArcMutexWeak — non-owning ArcMutex reference. */
typedef struct { ArcMutex *arc; } ArcMutexWeak;

/* arc_mutex_alloc — allocate a new ArcMutex-managed object.
 * Returns NULL if allocation or pthread_mutex_init fails.
 */
void *arc_mutex_alloc(size_t size, void (*destroy)(void *data));

/* arc_mutex_acquire — increment strong count under the mutex. */
void *arc_mutex_acquire(void *data);

/* arc_mutex_release — decrement strong count; calls destroy and frees at zero. */
void  arc_mutex_release(void *data);

/* arc_mutex_lock / arc_mutex_unlock — exclusive access to the data payload. */
void  arc_mutex_lock(void *data);
void  arc_mutex_unlock(void *data);

/* arc_mutex_downgrade — create an ArcMutexWeak from a live strong reference. */
ArcMutexWeak arc_mutex_downgrade(void *data);

/* arc_mutex_weak_upgrade — try to get a strong reference.
 * Locks the mutex to atomically check and increment count if alive.
 */
void *arc_mutex_weak_upgrade(ArcMutexWeak *weak);

/* arc_mutex_weak_release — drop a weak reference. */
void  arc_mutex_weak_release(ArcMutexWeak *weak);


#ifdef ARC_IMPLEMENTATION

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>

/* ── Arc implementation ──────────────────────────────────────────────────── */

void *arc_alloc(size_t size, void (*destroy)(void *data)) {
    Arc *arc = ARC_MALLOC(sizeof(Arc) + size);
    if (!arc) return NULL;
    atomic_init(&arc->count,      1);
    atomic_init(&arc->weak_count, 1);   /* phantom ref */
    arc->destroy = destroy;
    return arc + 1;
}

void *arc_acquire(void *data) {
    assert(data != NULL);
    Arc *arc    = (Arc *)data - 1;
    size_t old = atomic_fetch_add_explicit(&arc->count, 1, memory_order_relaxed);
    assert(old > 0 && "arc_acquire: strong count is zero (use-after-free)");
    return data;
}

void arc_release(void *data) {
    assert(data != NULL);
    Arc   *arc   = (Arc *)data - 1;
    size_t prev = atomic_fetch_sub_explicit(&arc->count, 1, memory_order_acq_rel);
    assert(prev > 0 && "arc_release: called more times than arc_acquire");
    if (prev == 1) {
        if (arc->destroy) arc->destroy(arc + 1);
        size_t wprev = atomic_fetch_sub_explicit(&arc->weak_count, 1, memory_order_acq_rel);
        if (wprev == 1) ARC_FREE(arc);
    }
}

ArcWeak arc_downgrade(void *data) {
    assert(data != NULL);
    Arc *arc = (Arc *)data - 1;
    assert(atomic_load_explicit(&arc->count, memory_order_relaxed) > 0
           && "arc_downgrade: object already destroyed");
    atomic_fetch_add_explicit(&arc->weak_count, 1, memory_order_relaxed);
    return (ArcWeak){ arc };
}

void *arc_weak_upgrade(ArcWeak *weak) {
    assert(weak != NULL && weak->arc != NULL && "arc_weak_upgrade: released weak pointer");
    Arc   *arc  = weak->arc;
    size_t old = atomic_load_explicit(&arc->count, memory_order_relaxed);
    while (old > 0) {
        if (atomic_compare_exchange_weak_explicit(
                &arc->count, &old, old + 1,
                memory_order_acquire,
                memory_order_relaxed))
            return arc + 1;
    }
    return NULL;
}

void arc_weak_release(ArcWeak *weak) {
    assert(weak != NULL && weak->arc != NULL && "arc_weak_release: already released");
    Arc   *arc   = weak->arc;
    weak->arc   = NULL;
    size_t prev = atomic_fetch_sub_explicit(&arc->weak_count, 1, memory_order_acq_rel);
    assert(prev > 0);
    if (prev == 1) ARC_FREE(arc);
}

/* ── ArcMutex implementation ─────────────────────────────────────────────── */

void *arc_mutex_alloc(size_t size, void (*destroy)(void *data)) {
    ArcMutex *arc = ARC_MALLOC(sizeof(ArcMutex) + size);
    if (!arc) return NULL;
    if (pthread_mutex_init(&arc->mutex, NULL) != 0) { ARC_FREE(arc); return NULL; }
    arc->count   = 1;
    atomic_init(&arc->weak_count, 1);   /* phantom ref */
    arc->destroy = destroy;
    return arc + 1;
}

void *arc_mutex_acquire(void *data) {
    assert(data != NULL);
    ArcMutex *arc = (ArcMutex *)data - 1;
    pthread_mutex_lock(&arc->mutex);
    assert(arc->count > 0 && "arc_mutex_acquire: strong count is zero (use-after-free)");
    arc->count++;
    pthread_mutex_unlock(&arc->mutex);
    return data;
}

void arc_mutex_release(void *data) {
    assert(data != NULL);
    ArcMutex *arc = (ArcMutex *)data - 1;
    pthread_mutex_lock(&arc->mutex);
    assert(arc->count > 0 && "arc_mutex_release: called more times than arc_mutex_acquire");
    arc->count--;
    int should_destroy = (arc->count == 0);
    pthread_mutex_unlock(&arc->mutex);
    if (should_destroy) {
        if (arc->destroy) arc->destroy(arc + 1);
        size_t wprev = atomic_fetch_sub_explicit(&arc->weak_count, 1, memory_order_acq_rel);
        if (wprev == 1) { pthread_mutex_destroy(&arc->mutex); ARC_FREE(arc); }
    }
}

void arc_mutex_lock(void *data) {
    assert(data != NULL);
    pthread_mutex_lock(&((ArcMutex *)data - 1)->mutex);
}

void arc_mutex_unlock(void *data) {
    assert(data != NULL);
    pthread_mutex_unlock(&((ArcMutex *)data - 1)->mutex);
}

ArcMutexWeak arc_mutex_downgrade(void *data) {
    assert(data != NULL);
    ArcMutex *arc = (ArcMutex *)data - 1;
    pthread_mutex_lock(&arc->mutex);
    assert(arc->count > 0 && "arc_mutex_downgrade: object already destroyed");
    atomic_fetch_add_explicit(&arc->weak_count, 1, memory_order_relaxed);
    pthread_mutex_unlock(&arc->mutex);
    return (ArcMutexWeak){ arc };
}

void *arc_mutex_weak_upgrade(ArcMutexWeak *weak) {
    assert(weak != NULL && weak->arc != NULL && "arc_mutex_weak_upgrade: released weak pointer");
    ArcMutex *arc = weak->arc;
    pthread_mutex_lock(&arc->mutex);
    int alive = (arc->count > 0);
    if (alive) arc->count++;
    pthread_mutex_unlock(&arc->mutex);
    return alive ? arc + 1 : NULL;
}

void arc_mutex_weak_release(ArcMutexWeak *weak) {
    assert(weak != NULL && weak->arc != NULL && "arc_mutex_weak_release: already released");
    ArcMutex *arc = weak->arc;
    weak->arc    = NULL;
    size_t prev  = atomic_fetch_sub_explicit(&arc->weak_count, 1, memory_order_acq_rel);
    assert(prev > 0);
    if (prev == 1) { pthread_mutex_destroy(&arc->mutex); ARC_FREE(arc); }
}

#endif /* ARC_IMPLEMENTATION */
#endif /* ARC_H_ */
