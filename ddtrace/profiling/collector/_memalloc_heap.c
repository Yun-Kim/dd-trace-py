#include <stdbool.h>

#define PY_SSIZE_T_CLEAN
#include "_memalloc_heap.h"
#include "_memalloc_tb.h"

typedef struct
{
    traceback_array_t allocs;
    /* Allocated memory counter in bytes */
    uint32_t allocated_memory;
    /* True if the heap tracker is frozen */
    bool frozen;
    /* Contains the ongoing heap allocation/deallocation while frozen */
    struct
    {
        traceback_array_t allocs;
        ptr_array_t frees;
    } freezer;
} heap_tracker_t;

static heap_tracker_t global_heap_tracker;

static void
heap_tracker_init(heap_tracker_t* heap_tracker)
{
    traceback_array_init(&heap_tracker->allocs);
    traceback_array_init(&heap_tracker->freezer.allocs);
    ptr_array_init(&heap_tracker->freezer.frees);
    heap_tracker->allocated_memory = 0;
    heap_tracker->frozen = false;
}

static void
heap_tracker_wipe(heap_tracker_t* heap_tracker)
{
    traceback_array_wipe(&heap_tracker->allocs);
    traceback_array_wipe(&heap_tracker->freezer.allocs);
    ptr_array_wipe(&heap_tracker->freezer.frees);
}

static void
heap_tracker_freeze(heap_tracker_t *heap_tracker)
{
    heap_tracker->frozen = true;
}

static void
heap_tracker_untrack_thawed(heap_tracker_t *heap_tracker, void* ptr)
{
    /* This search is O(n) where `n` is the number of tracked traceback,
       which is linearly linked to the heap size. This search could probably be
       optimized in a couple of ways:

       - sort the traceback in allocs by ptr so we can find the ptr in O(log2 n)
       - use a Bloom filter?

       That being said, we start iterating at the end of the array because most
       of the time this is where the untracked ptr is (the most recent object
       get de-allocated first usually). This might be a good enough
       trade-off. */
    foreach_reverse(tb, heap_tracker->allocs)
        if (ptr == (*tb)->ptr) {
            /* Free the traceback */
            traceback_free(*tb);
            traceback_array_remove(&heap_tracker->allocs, tb);
            break;
        }
}

static void
heap_tracker_thaw(heap_tracker_t *heap_tracker)
{
    /* Handle the free */
    foreach(ptr, heap_tracker->freezer.frees)
        heap_tracker_untrack_thawed(heap_tracker, *ptr);

    ptr_array_wipe(&heap_tracker->freezer.frees);

    /* Add the frozen allocs at the end */
    traceback_array_splice(&heap_tracker->allocs, heap_tracker->allocs.count,
                           0,
                           heap_tracker->freezer.allocs.tab,
                           heap_tracker->freezer.allocs.count);

    /* Reset the count to zero so we can reused and overwrite previous values */
    heap_tracker->freezer.allocs.count = 0;

    heap_tracker->frozen = false;
}

/* Public API */

void
memalloc_heap_tracker_init(void)
{
    heap_tracker_init(&global_heap_tracker);
}

void
memalloc_heap_tracker_deinit(void)
{
    heap_tracker_wipe(&global_heap_tracker);
}

void
memalloc_heap_tracker_freeze()
{
    heap_tracker_freeze(&global_heap_tracker);
}

void
memalloc_heap_tracker_thaw()
{
    heap_tracker_thaw(&global_heap_tracker);
}

void
memalloc_heap_untrack(void* ptr)
{
    if(global_heap_tracker.frozen)
        /* FIXME check for max capacity of frees */
        ptr_array_append(&global_heap_tracker.freezer.frees, ptr);
    else
        heap_tracker_untrack_thawed(&global_heap_tracker, ptr);
}

void
memalloc_heap_track(uint32_t heap_sample_size, uint16_t max_nframe, void* ptr, size_t size)
{
    /* Check for overflow */
    global_heap_tracker.allocated_memory = Py_MIN(global_heap_tracker.allocated_memory + size, MAX_HEAP_SAMPLE_SIZE);

    /* Check if we have enough sample or not */
    if (global_heap_tracker.allocated_memory < heap_sample_size)
        return;

    /* Cannot add more sample */
    if(global_heap_tracker.allocs.count >= TRACEBACK_ARRAY_MAX_COUNT)
        return;

    traceback_t* tb = memalloc_get_traceback(max_nframe, ptr, size);
    if (tb) {
        if(global_heap_tracker.frozen)
            traceback_array_append(&global_heap_tracker.freezer.allocs, tb);
        else
            traceback_array_append(&global_heap_tracker.allocs, tb);
        /* Reset the counter to 0 */
        global_heap_tracker.allocated_memory = 0;
    }
}

PyObject*
memalloc_heap()
{
    heap_tracker_freeze(&global_heap_tracker);

    PyObject* heap_list = PyList_New(global_heap_tracker.allocs.count);

    foreach(tb, global_heap_tracker.allocs)
    {
        PyObject* tb_and_size = PyTuple_New(2);
        PyTuple_SET_ITEM(tb_and_size, 0, traceback_to_tuple(*tb));
        PyTuple_SET_ITEM(tb_and_size, 1, PyLong_FromSize_t((*tb)->size));
        PyList_SET_ITEM(heap_list, traceback_array_indexof(&global_heap_tracker.allocs, tb), tb_and_size);
    }

    heap_tracker_thaw(&global_heap_tracker);

    return heap_list;
}
