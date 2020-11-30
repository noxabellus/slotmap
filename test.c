#define sm_SLOTMAP_IMPL
#include "slotmap.c"

#include <stdio.h>
#include <assert.h>

void map_dump (sm_Map const* m) {
  printf("map(%lu) [\n", m->slot_finder.length);

  for (size_t i = 0; i < m->slot_finder.length; ++ i) {
    uint32_t* slot_index = sm_vec_get(&m->slot_finder, i);
    sm_Slot* slot = sm_vec_get(&m->slots, *slot_index);
    int* value = sm_vec_get(&m->values, slot->index);

    printf(
      "  [%lu] / slots[%u] = { index: %u, generation: %u } -> %d\n",
      i, *slot_index, slot->index, slot->generation, *value
    );
  }

  printf("\n  freelist: { head: %ld", m->freelist_head);

  if (m->freelist_head != -1) {
    printf(", tail: %u } [\n", m->freelist_tail);

    uint32_t freenode = m->freelist_head;

    while (true) {
      sm_Slot* slot = sm_vec_get(&m->slots, freenode);
      printf("    [%u]\n", freenode);
      if (freenode != m->freelist_tail) {
         freenode = slot->index;
      } else {
        break;
      }
    }

    printf("  ]\n");
  } else {
    printf(" }\n");
  }

  printf("]\n");
}

void key_dump (sm_Key const* k, size_t len) {
  printf("keys(%lu) [\n", len);

  for (size_t i = 0; i < len; ++ i) {
    printf("  [%lu] = { index: %u, generation: %u }\n", i, k[i].index, k[i].generation);
  }

  printf("]\n");
}


int main () {
  sm_Map map = sm_map_new(sizeof(int), 4);

  sm_Key keys [16];

  for (int i = 0; i < 16; ++ i) {
    keys[i] = sm_map_insert(&map, &i);
  }

  // key_dump(keys, 16);
  map_dump(&map);

  int removed_5 = -1;
  int removed_12 = -1;
  assert(sm_map_remove(&map, keys[5], &removed_5));
  assert(sm_map_remove(&map, keys[12], &removed_12));

  map_dump(&map);

  printf("%d == %d?\n", removed_5, 5);
  assert(removed_5 == 5);
  printf("%d == %d?\n", removed_12, 12);
  assert(removed_12 == 12);

  keys[5] = sm_map_insert(&map, (int[]) {5});
  map_dump(&map);

  keys[12] = sm_map_insert(&map, (int[]) {12});
  map_dump(&map);

  int* new_5 = sm_map_get(&map, keys[5]);
  int* new_12 = sm_map_get(&map, keys[12]);

  printf("%d == %d?\n", *new_5, 5);
  assert(*new_5 == 5);

  printf("%d == %d?\n", *new_12, 12);
  assert(*new_12 == 12);

  // key_dump(keys, 16);
}