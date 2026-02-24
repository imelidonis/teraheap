#ifndef SLINKEDLIST_H
#define SLINKEDLIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/*
 * Comparator function pointer type.
 * Must return 0 when the two values are considered equal.
 * Used by sll_delete() and sll_find().
 */
typedef int (*sll_cmp_fn)(const void *a, const void *b);

/* Opaque node structure. */
typedef struct SLLNode {
    void           *value;
    struct SLLNode *next;
} SLLNode;

/* Singly linked list handle. */
typedef struct {
    SLLNode    *head;
    size_t      size;
    sll_cmp_fn  cmp;
} SLinkedList;

/*
 * Opaque iterator handle. Declare one on the stack and pass it to
 * sll_iter_init() before use. Do not modify its fields directly.
 */
typedef struct {
    SLLNode *current;
} SLLIterator;

/*
 * sll_create()
 *   Allocates and initialises a new singly linked list.
 *
 *   @param cmp  Comparator used by sll_delete() and sll_find().
 *               Must not be NULL.
 *   @return     Pointer to the new list, or NULL on allocation failure.
 */
SLinkedList *sll_create(sll_cmp_fn cmp);

/*
 * sll_push_front()
 *   Inserts a value at the front of the list in O(1).
 *   Ownership of the pointed-to data is NOT transferred; the list only
 *   stores the pointer.
 *
 *   @param list   Must not be NULL.
 *   @param value  Pointer to the value to store (may be NULL).
 *   @return       0 on success, -1 on allocation failure.
 */
int sll_push_front(SLinkedList *list, void *value);

/*
 * sll_delete()
 *   Removes the FIRST node whose value compares equal to @value
 *   (according to the comparator supplied to sll_create()).
 *   The stored pointer itself is NOT freed; the caller is responsible for
 *   managing the lifetime of the pointed-to data.
 *
 *   @param list   Must not be NULL.
 *   @param value  Value to search for.
 *   @return       0 if a node was removed, -1 if no matching node was found.
 */
int sll_delete(SLinkedList *list, const void *value);

/*
 * sll_find()
 *   Searches for the first node whose value compares equal to @value.
 *
 *   @param list   Must not be NULL.
 *   @param value  Value to search for.
 *   @return       The stored pointer if found, NULL otherwise.
 *                 Note: a stored NULL pointer is indistinguishable from
 *                 "not found"; use sll_contains() when storing NULLs.
 */
void *sll_find(const SLinkedList *list, const void *value);

/*
 * sll_contains()
 *   Returns 1 if at least one node compares equal to @value, 0 otherwise.
 *   Prefer this over sll_find() when NULL is a valid stored value.
 *
 *   @param list   Must not be NULL.
 *   @param value  Value to search for.
 */
int sll_contains(const SLinkedList *list, const void *value);

/*
 * sll_size()
 *   Returns the number of elements currently in the list in O(1).
 */
size_t sll_size(const SLinkedList *list);

/*
 * sll_destroy()
 *   Frees all nodes and the list structure itself.
 *   The stored data pointers are NOT freed.
 *   After this call @list must not be used.
 *
 *   @param list  May be NULL (no-op).
 */
void sll_destroy(SLinkedList *list);

/*
 * sll_iter_init()
 *   Initialises @it to point at the first element of @list.
 *   Must be called before the first sll_iter_next() call.
 *   Reuse the same iterator to restart traversal from the beginning.
 *
 *   @param list  Must not be NULL.
 *   @param it    Must not be NULL.
 */
void sll_iter_init(const SLinkedList *list, SLLIterator *it);

/*
 * sll_iter_has_next()
 *   Returns 1 if there is a current element to retrieve, 0 if the
 *   iterator has been exhausted.
 *
 *   @param it  Must not be NULL.
 */
int sll_iter_has_next(const SLLIterator *it);

/*
 * sll_iter_next()
 *   Returns the value stored in the current node and advances the
 *   iterator to the next element.
 *   Calling this when sll_iter_has_next() returns 0 is undefined behaviour.
 *
 *   @param it  Must not be NULL.
 *   @return    The void * value stored in the current node.
 */
void *sll_iter_next(SLLIterator *it);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SLINKEDLIST_H */
