#ifndef sm_SLOTMAP_H
#define sm_SLOTMAP_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>


#if defined(_WIN32) && defined(sm_SHARED_LIB)
  #ifdef sm_SLOTMAP_IMPL
    #define sm_API __declspec(dllexport)
  #else
    #define sm_API __declspec(dllimport)
  #endif
#else
  #define sm_API
#endif


typedef struct {
  char* ptr;
  size_t length;
  size_t capacity;
  size_t elem_size;
} sm_Vec;

sm_API sm_Vec sm_vec_new (size_t elem_size, size_t capacity);
sm_API void sm_vec_del (sm_Vec* v);

sm_API void* sm_vec_get (sm_Vec const* v, size_t index);
sm_API void* sm_vec_push (sm_Vec* v, void const* val);
sm_API void sm_vec_swap_remove (sm_Vec* v, size_t index, void* out_val);


struct sm_Pair {
  uint32_t index;
  uint32_t generation;
};

typedef struct sm_Pair sm_Slot;
typedef struct sm_Pair sm_Key;


typedef struct {
  // Maps Keys to Values
  sm_Vec slots;
  // Maps Values to Slots (required to redirect slots when removing values)
  sm_Vec slot_finder;
  // Actual data storage
  sm_Vec values;
  // The index of the first free slot (or -1 if none)
  int64_t freelist_head;
  // The index of the last free slot
  // (this removes the need to mark whether an index in a slot is a freelist node or not)
  uint32_t freelist_tail;
} sm_Map;

sm_API sm_Map sm_map_new (size_t value_size, size_t capacity);
sm_API void sm_map_del (sm_Map* m);

sm_API sm_Key sm_map_insert (sm_Map* m, void const* val);
sm_API void* sm_map_get (sm_Map* m, sm_Key key);
sm_API bool sm_map_remove (sm_Map* m, sm_Key key, void* out_val);

#endif



#ifdef sm_SLOTMAP_IMPL
#ifndef sm_SLOTMAP_C
#define sm_SLOTMAP_C

sm_API sm_Vec sm_vec_new (size_t elem_size, size_t capacity) {
  sm_Vec v;
  v.ptr = malloc(capacity * elem_size);
  v.length = 0;
  v.elem_size = elem_size;
  v.capacity = capacity;

  return v;
}

sm_API void sm_vec_del (sm_Vec* v) {
  free(v->ptr);
}

sm_API void* sm_vec_get (sm_Vec const* v, size_t index) {
  return v->ptr + (index * v->elem_size);
}

sm_API void* sm_vec_push (sm_Vec* v, void const* val) {
  // grow allocation if needed
  if (v->length > v->capacity) {
    while(v->length > v->capacity) v->capacity *= 2;

    v->ptr = realloc(v->ptr, v->capacity * v->elem_size);  
  }  

  // copy data if needed
  void* ptr = sm_vec_get(v, v->length);
  if (val != NULL) {
    memcpy(ptr, val, v->elem_size);
  }

  // update length and return address of moved data
  ++ v->length;

  return ptr;
}

sm_API void sm_vec_swap_remove (sm_Vec* v, size_t index, void* out_val) {
  // pre-decrement avoids `v-length - 1` below
  -- v->length;

  void* ptr = sm_vec_get(v, index);
  // copy data out if needed
  if (out_val != NULL) {
    memcpy(out_val, ptr, v->elem_size);
  }

  // move a value from the end of the vec to fill the hole if there is one
  if (index < v->length) {
    memcpy(ptr, sm_vec_get(v, v->length), v->elem_size);
  }
}





sm_API sm_Map sm_map_new (size_t value_size, size_t capacity) {
  sm_Map m;
  m.slots = sm_vec_new(sizeof(sm_Slot), capacity);
  m.slot_finder = sm_vec_new(sizeof(uint32_t), capacity);
  m.values = sm_vec_new(value_size, capacity);
  m.freelist_head = -1;

  return m;
}

sm_API void sm_map_del (sm_Map* m) {
  sm_vec_del(&m->slots);
  sm_vec_del(&m->slot_finder);
  sm_vec_del(&m->values);
}

sm_API sm_Key sm_map_insert (sm_Map* m, void const* val) {
  sm_Key key;
  sm_Slot* slot;

  if (m->freelist_head != -1) { // get a slot from the freelist
    key.index = m->freelist_head;

    slot = sm_vec_get(&m->slots, m->freelist_head);

    // update the freelist
    if (m->freelist_tail != m->freelist_head) {
      m->freelist_head = slot->index;
    } else {
      m->freelist_head = -1;
    }
  } else { // append a new slot
    key.index = m->slots.length;

    slot = sm_vec_push(&m->slots, NULL);
    slot->generation = 1;
  }
  
  // update the slot and bind the key generation
  slot->index = m->values.length;
  key.generation = slot->generation;

  // append the new value and slot index
  sm_vec_push(&m->values, val);
  sm_vec_push(&m->slot_finder, &key.index);

  return key;
}

sm_API void* sm_map_get (sm_Map* m, sm_Key key) {
  sm_Slot* slot = sm_vec_get(&m->slots, key.index);

  return (slot->generation == key.generation? sm_vec_get(&m->values, slot->index) : NULL);
}

sm_API bool sm_map_remove (sm_Map* m, sm_Key key, void* out_val) {
  sm_Slot* slot = sm_vec_get(&m->slots, key.index);

  if (slot->generation != key.generation) {
    return false;
  }

  // Slot generation updates as value is removed to prevent use after free
  ++ slot->generation;
  sm_vec_swap_remove(&m->values, slot->index, out_val);

  // We need to remap the slot who's value has moved to fill the hole left by the old value,
  // so after removing the old slot finder entry, we use the new index that was swapped in to locate it
  sm_vec_swap_remove(&m->slot_finder, slot->index, NULL);
  uint32_t* moved_slot_index = sm_vec_get(&m->slot_finder, slot->index);

  // Lookup the slot who's value moved, and set its index to that of the old value
  sm_Slot* moved_slot = sm_vec_get(&m->slots, *moved_slot_index);
  moved_slot->index = slot->index;

  // update the freelist
  if (m->freelist_head != -1) {
    // store the existing the head in the newly freed slot
    slot->index = m->freelist_head;
  } else {
    // if there was no existing freelist then
    // freelist tail is in an invalid state, so we clear it
    m->freelist_tail = key.index;
  }

  m->freelist_head = key.index;

  return true;
}

#endif
#endif