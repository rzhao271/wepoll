#include <assert.h>
#include <stdlib.h>

#include "error.h"
#include "nt.h"
#include "reflock.h"
#include "util.h"
#include "win.h"

/* clang-format off */
#define REFLOCK__REF          ((long) 0x00000001UL)
#define REFLOCK__REF_MASK     ((long) 0x0fffffffUL)
#define REFLOCK__DESTROY      ((long) 0x10000000UL)
#define REFLOCK__DESTROY_MASK ((long) 0xf0000000UL)
#define REFLOCK__POISON       ((long) 0x300dead0UL)
/* clang-format on */

static void* reflock__wait_object = NULL;

void reflock_init(reflock_t* reflock) {
  reflock->state = 0;
}

static void reflock__signal_event(void* address) {
  reflock__wait_object = NULL;
  WakeByAddressSingle(address);
}

static void reflock__await_event(void* address) {
  reflock__wait_object = address;
  do {
    BOOL status = WaitOnAddress(address, reflock__wait_object, sizeof(void*), INFINITE);
    if (status != TRUE)
      abort();
  } while (reflock__wait_object == address);
}

void reflock_ref(reflock_t* reflock) {
  long state = InterlockedAdd(&reflock->state, REFLOCK__REF);

  /* Verify that the counter didn't overflow and the lock isn't destroyed. */
  assert((state & REFLOCK__DESTROY_MASK) == 0);
  unused_var(state);
}

void reflock_unref(reflock_t* reflock) {
  long state = InterlockedAdd(&reflock->state, -REFLOCK__REF);

  /* Verify that the lock was referenced and not already destroyed. */
  assert((state & REFLOCK__DESTROY_MASK & ~REFLOCK__DESTROY) == 0);

  if (state == REFLOCK__DESTROY)
    reflock__signal_event(reflock);
}

void reflock_unref_and_destroy(reflock_t* reflock) {
  long state =
      InterlockedAdd(&reflock->state, REFLOCK__DESTROY - REFLOCK__REF);
  long ref_count = state & REFLOCK__REF_MASK;

  /* Verify that the lock was referenced and not already destroyed. */
  assert((state & REFLOCK__DESTROY_MASK) == REFLOCK__DESTROY);

  if (ref_count != 0)
    reflock__await_event(reflock);

  state = InterlockedExchange(&reflock->state, REFLOCK__POISON);
  assert(state == REFLOCK__DESTROY);
}
