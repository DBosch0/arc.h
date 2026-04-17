/* rc.h — Single-header intrusive reference counting for C
 * =========================================================
 *
 * NOT thread-safe.  For thread-safe reference counting see arc.h.
 * 
 * Requires: -std=c11  (_Alignas)
 *
 * MEMORY LAYOUT
 *   Every allocation is one contiguous block:  [Rc header | user data]
 *   rc_alloc() returns a pointer to the user-data region.  The header
 *   lives immediately before it at (Rc*)data - 1.
 *
 * STRONG REFERENCES
 *   rc_alloc    — allocate; initial strong count = 1.
 *   rc_acquire  — increment strong count; extend lifetime.
 *   rc_release  — decrement strong count; triggers destroy callback + free at zero.
 *
 * WEAK REFERENCES
 *   A weak reference does not keep the object alive.  Use rc_downgrade()
 *   to create one from a live strong reference, then rc_weak_upgrade() to
 *   try converting it back.  Upgrade returns NULL once the object is gone.
 *   Typical use: break reference cycles between parent and child objects.
 *
 *   rc_downgrade    — create an RcWeak from a live strong reference.
 *   rc_weak_upgrade — attempt to obtain a new strong reference.
 *   rc_weak_release — drop a weak reference.
 *
 * CUSTOM ALLOCATOR
 *   Define both macros before inlcuding this header:
 *
 *     #define RC_MALLOC(sz)      my_malloc(sz)
 *     #define RC_FREE(p)         my_free(p)
 *     #include "rc.h"
 *
 *   Defining only one is an error.  When none are defined the
 *   library falls back to malloc / free from <stdlib.h>.
 *
 * USAGE
 *   In exactly one translation unit:
 *     #define RC_IMPLEMENTATION
 *     #include "rc.h"
 *   In all other translation units:
 *     #include "rc.h"
 *
 * EXAMPLE
 *   typedef struct { int x, y; } Point;
 *
 *   Point *p = rc_alloc(sizeof(Point), NULL);
 *   p->x = 1; p->y = 2;
 *
 *   Point *alias = rc_acquire(p);    // count → 2
 *   rc_release(p);                   // count → 1
 *   rc_release(alias);               // count → 0; memory freed
 *
 *   // Weak pointer example:
 *   Point *q = rc_alloc(sizeof(Point), NULL);
 *   RcWeak w  = rc_downgrade(q);     // weak ref; strong count stays 1
 *   rc_release(q);                   // count → 0; object destroyed
 *   Point *r  = rc_weak_upgrade(&w); // NULL — object is gone
 *   rc_weak_release(&w);
 */

#ifndef RC_H_
#define RC_H_

#include <stddef.h>

/* ── Allocator macros ────────────────────────────────────────────────────────
 * Must define both or none; partial definition is a compile error.
 */
#if defined(RC_MALLOC) && defined(RC_FREE)
    /* all provided — ok */
#elif !defined(RC_MALLOC) && !defined(RC_FREE)
    /* none provided — defaults applied below */
#else
#   error "Must define all or none of RC_MALLOC, and RC_FREE."
#endif

#ifndef RC_MALLOC
#   define RC_MALLOC(sz)     malloc(sz)
#   define RC_FREE(p)        free(p)
#endif

/* Rc — reference-counting header stored immediately before the user data.
 *
 * _Alignas(max_align_t) on the first field forces sizeof(Rc) to be a multiple
 * of max_align_t so that user data following the header is correctly aligned
 * on all platforms, including 32-bit (where 3×4 = 12 would misalign doubles).
 *   64-bit: sizeof == 32   32-bit: sizeof == 16
 */
typedef struct {
    _Alignas(max_align_t) size_t  count;    /* strong reference count                     */
    size_t  weak_count;                     /* weak refs + 1 phantom held while count > 0 */
    void   (*destroy)(void *data);
} Rc;

/* RcWeak — non-owning reference that survives object destruction.
 * rc field is set to NULL by rc_weak_release(); treat as opaque.
 */
typedef struct {
    Rc *rc;
} RcWeak;

/* rc_alloc — allocate a new reference-counted object.
 *
 *   size    : byte size of the user data region (may be 0).
 *   destroy : optional callback invoked when the strong count reaches zero;
 *             receives a pointer to the user data.  Pass NULL if not needed.
 *
 * Returns a pointer to the user data, or NULL if allocation fails.
 * The initial strong count is 1.
 */
void *rc_alloc(size_t size, void (*destroy)(void *data));

/* rc_acquire — increment the strong reference count.
 *
 *   data : must not be NULL; must have a live strong reference (count > 0).
 *
 * Returns data unchanged.
 */
void *rc_acquire(void *data);

/* rc_release — decrement the strong reference count.
 *
 * When the count reaches zero the destroy callback is invoked (if set), and
 * the backing allocation is freed once all weak references are also released.
 *
 *   data : must not be NULL; must not be released more times than acquired.
 */
void rc_release(void *data);

/* rc_downgrade — create a weak reference from a live strong reference.
 *
 * The weak reference does not affect the strong count.  The object may be
 * destroyed while the weak reference is still held.
 *
 *   data : must not be NULL; the strong count must be > 0.
 *
 * Returns an RcWeak by value.  Must be released with rc_weak_release().
 */
RcWeak rc_downgrade(void *data);

/* rc_weak_upgrade — attempt to obtain a new strong reference.
 *
 * If the object is still alive (strong count > 0), increments the strong
 * count and returns a pointer to the user data.  Otherwise returns NULL.
 *
 *   weak : must not be NULL; weak->rc must not be NULL (not yet released).
 */
void *rc_weak_upgrade(RcWeak *weak);

/* rc_weak_release — drop a weak reference.
 *
 * Sets weak->rc to NULL.  If this was the last weak reference and the object
 * has already been destroyed, the backing allocation is freed here.
 *
 *   weak : must not be NULL; must not have been released already.
 */
void rc_weak_release(RcWeak *weak);


#ifdef RC_IMPLEMENTATION

#include <assert.h>
#include <stdlib.h>

void *rc_alloc(size_t size, void (*destroy)(void *data)) {
    Rc *rc = RC_MALLOC(sizeof(Rc) + size);
    if (!rc) return NULL;
    rc->count      = 1;
    rc->weak_count = 1;   /* phantom ref; released when strong count hits 0 */
    rc->destroy    = destroy;
    return rc + 1;
}

void *rc_acquire(void *data) {
    assert(data != NULL);
    Rc *rc = (Rc *)data - 1;
    assert(rc->count > 0 && "rc_acquire: strong count is zero (use-after-free)");
    rc->count++;
    return data;
}

void rc_release(void *data) {
    assert(data != NULL);
    Rc *rc = (Rc *)data - 1;
    assert(rc->count > 0 && "rc_release: called more times than rc_acquire");
    rc->count--;
    if (rc->count == 0) {
        if (rc->destroy) rc->destroy(rc + 1);
        rc->weak_count--;           /* drop the phantom weak ref */
        if (rc->weak_count == 0)
            RC_FREE(rc);
    }
}

RcWeak rc_downgrade(void *data) {
    assert(data != NULL);
    Rc *rc = (Rc *)data - 1;
    assert(rc->count > 0 && "rc_downgrade: object already destroyed");
    rc->weak_count++;
    return (RcWeak){ rc };
}

void *rc_weak_upgrade(RcWeak *weak) {
    assert(weak != NULL && weak->rc != NULL && "rc_weak_upgrade: released weak pointer");
    Rc *rc = weak->rc;
    if (rc->count == 0) return NULL;
    rc->count++;
    return rc + 1;
}

void rc_weak_release(RcWeak *weak) {
    assert(weak != NULL && weak->rc != NULL && "rc_weak_release: already released");
    Rc *rc   = weak->rc;
    weak->rc = NULL;
    assert(rc->weak_count > 0);
    rc->weak_count--;
    if (rc->weak_count == 0)
        RC_FREE(rc);
}

#endif /* RC_IMPLEMENTATION */
#endif /* RC_H_ */
