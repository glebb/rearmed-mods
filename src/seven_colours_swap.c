// Seven-String Guitar (seven.h): the low B swap of the colours part (seven_colours_swap.h). Pure logic.
#include <string.h>

#include "seven_colours_swap.h"

static uint8_t *pointer_at(const seven_swap_tables *t, int mode, int kind, int column) {
  return t->ptrs + (size_t)((mode * SEVEN_SWAP_KINDS + kind) * SEVEN_SWAP_COLUMNS + column) * (size_t)t->ptr_size;
}

static uint8_t *v_at(const seven_swap_tables *t, int mode, int kind, int column) {
  return t->uvs + (size_t)((mode * SEVEN_SWAP_KINDS + kind) * SEVEN_SWAP_COLUMNS + column) * 4;
}

static uint64_t get_ptr(const seven_swap_tables *t, const uint8_t *at) {
  if (t->ptr_size == 8) {
    uint64_t v;
    memcpy(&v, at, 8);
    return v;
  }
  uint32_t v;
  memcpy(&v, at, 4);
  return v;
}

static void put_ptr(const seven_swap_tables *t, uint8_t *at, uint64_t v) {
  if (t->ptr_size == 8) {
    memcpy(at, &v, 8);
  } else {
    uint32_t w = (uint32_t)v;
    memcpy(at, &w, 4);
  }
}

static int is_uv_kind(const int *kinds, int n, int kind) {
  for (int i = 0; i < n; i++)
    if (kinds[i] == kind) return 1;
  return 0;
}

void seven_swap_apply(seven_swap *s, const seven_swap_tables *t, const void *owner, const int *uv_kinds, int n_uv) {
  if (s->swapped && s->owner == owner) return;
  for (int m = 0; m < SEVEN_SWAP_MODES; m++)
    for (int k = 0; k < SEVEN_SWAP_KINDS; k++) {
      uint8_t *p4 = pointer_at(t, m, k, SEVEN_SWAP_LOW_B), *v4 = v_at(t, m, k, SEVEN_SWAP_LOW_B);
      s->saved_ptr[m][k] = get_ptr(t, p4);
      memcpy(&s->saved_uv[m][k], v4, 4);
      put_ptr(t, p4, get_ptr(t, pointer_at(t, m, k, SEVEN_SWAP_SOURCE)));
      if (is_uv_kind(uv_kinds, n_uv, k)) memcpy(v4, v_at(t, m, k, SEVEN_SWAP_SOURCE), 4);
      s->swapped_ptr[m][k] = get_ptr(t, p4);
      memcpy(&s->swapped_uv[m][k], v4, 4);
    }
  s->swapped = 1;
  s->owner = owner;
}

void seven_swap_restore(seven_swap *s, const seven_swap_tables *t, const void *owner) {
  if (!s->swapped) return;
  if (s->owner == owner)
    for (int m = 0; m < SEVEN_SWAP_MODES; m++)
      for (int k = 0; k < SEVEN_SWAP_KINDS; k++) {
        uint8_t *p4 = pointer_at(t, m, k, SEVEN_SWAP_LOW_B), *v4 = v_at(t, m, k, SEVEN_SWAP_LOW_B);
        uint32_t uv;
        if (get_ptr(t, p4) == s->swapped_ptr[m][k]) put_ptr(t, p4, s->saved_ptr[m][k]);
        memcpy(&uv, v4, 4);
        if (uv == s->swapped_uv[m][k]) memcpy(v4, &s->saved_uv[m][k], 4);
      }
  s->swapped = 0;
  s->owner = NULL;
}
