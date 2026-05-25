#include "../include/sllist.h"

#include <assert.h>
#include <stdlib.h>

static SLLNode *node_alloc(void *value) {
  SLLNode *n = (SLLNode *)malloc(sizeof(SLLNode));
  if (n) {
    n->value = value;
    n->next  = NULL;
  }
  return n;
}

SLinkedList *sll_create(sll_cmp_fn cmp) {
  assert(cmp != NULL);

  SLinkedList *list = (SLinkedList *)malloc(sizeof(SLinkedList));
  if (!list)
    return NULL;

  list->head = NULL;
  list->size = 0;
  list->cmp  = cmp;
  return list;
}

int sll_push_front(SLinkedList *list, void *value) {
  assert(list != NULL);

  SLLNode *n = node_alloc(value);
  if (!n)
    return -1;

  n->next    = list->head;
  list->head = n;
  list->size++;
  return 0;
}

int sll_delete(SLinkedList *list, const void *value) {
  assert(list != NULL);

  SLLNode *prev = NULL;
  SLLNode *curr = list->head;

  while (curr) {
    if (list->cmp(curr->value, value) == 0) {
      /* Unlink the node */
      if (prev)
        prev->next = curr->next;
      else
        list->head = curr->next;

      free(curr);
      list->size--;
      return 0;
    }
    prev = curr;
    curr = curr->next;
  }

  return -1; /* not found */
}

void *sll_find(const SLinkedList *list, const void *value) {
  assert(list != NULL);

  for (SLLNode *curr = list->head; curr; curr = curr->next) {
    if (list->cmp(curr->value, value) == 0)
      return curr->value;
  }
  return NULL;
}

int sll_contains(const SLinkedList *list, const void *value) {
  assert(list != NULL);

  for (SLLNode *curr = list->head; curr; curr = curr->next) {
    if (list->cmp(curr->value, value) == 0)
      return 1;
  }
  return 0;
}

size_t sll_size(const SLinkedList *list) {
  assert(list != NULL);
  return list->size;
}

void sll_destroy(SLinkedList *list) {
  if (!list)
    return;

  SLLNode *curr = list->head;
  while (curr) {
    SLLNode *next = curr->next;
    free(curr);
    curr = next;
  }

  free(list);
}

void sll_iter_init(const SLinkedList *list, SLLIterator *it) {
    assert(list != NULL);
    assert(it   != NULL);
    it->current = list->head;
}

int sll_iter_has_next(const SLLIterator *it) {
    assert(it != NULL);
    return (it->current != NULL);
}

void *sll_iter_next(SLLIterator *it) {
    assert(it != NULL);
    assert(it->current != NULL); /* caller must check sll_iter_has_next() first */

    void *value  = it->current->value;
    it->current  = it->current->next;
    return value;
}
