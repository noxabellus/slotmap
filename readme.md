# `slotmap.c`

A minimal implementation of a dense slotmap, under a permissive [MIT license](./LICENSE)


## Contents

- [About slotmaps](#about-slotmaps)
  - [Pros](#pros)
  - [Cons](#cons)
- [Implementation](#implementation)
  - [`Vec`](#vec)
  - [`Map`](#map)
  - [`Slot` & `Key`](#slot--key)
  - [Freelist](#freelist)
  - [Behavior](#behavior)
    - [Creation](#creation)
    - [Cleanup](#cleanup)
    - [Insertion](#insertion)
    - [Lookup](#lookup)
    - [Removal](#removal)
    - [Extras](#extras)
  - [Limitations](#limitations)
- [Usage](#usage)
  - [Include](#include)
  - [Implement](#implement)
    - [Inline](#inline)
    - [Static](#static)
  - [Dynamic/Shared Library](#dynamicshared-library)
    - [Linux](#linux)
    - [Windows](#windows)
- [Example](#example)


## About slotmaps

Slotmaps provide index-like access to values in an array with great data locality, where the "index" is valid through insertions/deletions/reallocation, and protect against use-after-free style errors, while introducing only a single level of indirection.


### Pros:

+ Cheap, single indirection using a single branch and basic pointer arithmetic
+ Stable indexing
+ Tightly packed values
+ Protection against invalid access of reused & deleted values
+ 64 bit key type fits in a register


### Cons:

+ Memory overhead of ~12 bytes per value
+ Indirection array cannot shrink
+ Key collision possible after `UINT32_MAX` reuses
+ While minimal, indirection of any kind does have a cost


## Implementation


### `Vec`

An extremely minimal implementation of a dynamic array, most likely not suitable for purposes other than its use in the slotmap


### `Map`

The slotmap defined here has 3 internal dynamic arrays:
+ `slots` - All the `Slot`s in the slotmap; once a new slot is added it is never removed
+ `slot_finder` - Indices of slots associated with values below
+ `values` - The user's actual array elements

Additionally, there are two fields to help managing the free slots:
+ `freelist_head` - The index of the first free `Slot` in the pseudo linked list of available slots, or `-1` if there aren't any
+ `freelist_tail` - The index of the last free `Slot` in the list, if `freelist_head` is valid, otherwise undefined


### `Slot` & `Key`

The slot and key structures are just aliases for `Pair`, which has two fields:
+ `index` - In a `Slot`, the index is an offset into the `values` array; while in a `Key` the index is an offset into the `slots` array
+ `generation` - Binds a `Key` and a `Slot` together: if a key's slot has a matching generation, it is safe to complete the indirection and provide access to the slot's *value*


### Freelist

The *freelist* is referred to here as a "pseudo linked list" because it is implemented by repurposing the `index` field of `Slot`. When a slot becomes free, its old *value*'s `index` is overwritten with the index of the next slot in the *freelist*, if there is one. Because there is no `NULL` for unsigned indices, the *freelist* tail is tracked, to avoid misinterpreting *value* indices as *freelist* indices


### Behavior

#### Creation

Creating a new slotmap is done with `new`, and initializing a map into a state other than one similar to what new does is likely to produce UB


#### Cleanup

Cleanup a slotmap's memory allocations with `del`. Alternatively, you can manually use the `Vec`'s del function to free only the `slots` and `slot_finder`, and take the `values` array for use elsewhere.


#### Insertion

When adding a new *value* into the slotmap, the value will always be placed at the end of the `values` array. The `Slot` used will be either a brand new one at the end of the slots array, or the last one freed if it is available in the *freelist*. After a slot for the value has been acquired, the index of the slot is inserted parallel to the value in the `slot_finder` array, to facilitate remapping the slot during relocation of the value. A `Key` is returned with the index of the slot, and the slot's `generation`


#### Lookup

To lookup a *value*, the `Key` returned at insertion must be provided. The key's index is used to find a `Slot`, and then the `generation` values of the key and slot are compared. If they are equal, the slot's `index` is used to find the value, and a pointer is returned. If the key was invalid, `NULL` is returned


#### Removal

When a *value* needs to be removed, its `Key` must be provided, and lookup occurs as normal. If the key was invalid, `false` is returned here. Otherwise, the value and its parallel slot lookup index in the `slot_finder` array are *swap removed*. In swap removal, the element from the end of the array is moved to overwrite the removed data. This keeps the array tightly packed with the minimal amount of memory copied. After swapping, the end slot lookup index is used to point assign associated `Slot` the index the end value was moved to. Finally, the freed slot is added to the *freelist*, and `true` is returned. As a convenience, this implementation allows providing an out pointer to copy the removed value to before it is overwritten


#### Extras

Iteration can be done in a normal fashion over the `values` Vec's internal buffer; Simply cast it to the value type and iterate up to `map.values.length`. If `Key`/*value* pair iteration is desired, the `slot_finder` array is fully parallel to `values` and may be used to create valid keys by following the slot indices to get the `generation`

If a *value* needs to be aware of its own `Key`, you can pass `NULL` for the value pointer when inserting into the map, and initialize the value after acquiring the key


### Limitations

This code focuses on simplicity of implementation over safety; as such there are no bounds checks, null checks, or other guardrails in place, and invalid keys, uninitialized structures, `NULL` pointers and the like can cause all manner of undefined behavior. Use with care and validate yourself where needed. This code also does not make any attempt to free extra memory once it has been acquired (outside of `map_del`), though it is perfectly valid to do so on the values array.


## Usage

### Include

The code is provided as a single file library, simply include the file as a header where you would like to use slotmaps
```c
// my_file.h
#include "slotmap.c"
```


### Implement

#### Inline

To inline the implementation into your source, include the file with a proceeding `#define sm_SLOTMAP_IMPL` somewhere in your implementation code
```c
// my_file.c
#define sm_SLOTMAP_IMPL
#include "slotmap.c"
```


#### Static

To compile a free-standing library, you can just define `sm_SLOTMAP_IMPL` through the command line
```sh
clang -Dsm_SLOTMAP_IMPL slotmap.c
# ...
clang your_source/*.c slotmap.o
```


### Dynamic/Shared Library

To compile a shared library or dll, you must define `sm_SLOTMAP_IMPL`, and on windows you must also define `sm_SHARED_LIB` while compiling and including as a header 

#### Linux:

<!--TODO: add example for static linking linux so file-->
```sh
clang -Dsm_SLOTMAP_IMPL slotmap.c -fPIC -shared -oslotmap.so
```


#### Windows:

```bat
clang-cl -Dsm_SLOTMAP_IMPL -Dsm_SHARED_LIB /LD slotmap.c
:: ...
clang-cl your_source/*.c slotmap.lib
```
```c
// my_windows_file.h
#define sm_SHARED_LIB
#include "slotmap.c"
```


## Example

```c
// Create a new slotmap capable of holding integers, with an initial capacity for 16
sm_Map map = sm_map_new(sizeof(int), 16);

// Insert two values and get their keys
sm_Key k_1 = sm_map_insert(&map, (int[]){1});
sm_Key k_2 = sm_map_insert(&map, (int[]){2});

// Remove the first value
int v_1;
assert(sm_map_remove(&map, k_1, &v_1));
assert(v_1 == 1);

// Check that the second value is still accessible
int* v_2 = sm_map_get(&map, k_2);
assert(*v_2 == 2);

// Cleanup the heap allocations in the slotmap
sm_map_del(&map);
```