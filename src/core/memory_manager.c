#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <assert.h>
#include "memory_manager.h"

#define CACHE_LINE_SIZE 64

struct entry {
  struct entry *next, *prev;
  void *address;
  size_t size;
  int available;
};

static size_t cache_alignment(size_t size) {
  return (size + CACHE_LINE_SIZE - 1) & (ULONG_MAX - (CACHE_LINE_SIZE - 1));
}

int ext_mpi_dmalloc_init(void **root, void *memory_chunk, size_t max_bytes) {
  struct entry *start;
  void *p;
  p = (void *)(((unsigned long)(memory_chunk + CACHE_LINE_SIZE - 1)) & (ULONG_MAX - (CACHE_LINE_SIZE - 1)));
  max_bytes = (max_bytes - (p - memory_chunk)) & (ULONG_MAX - (CACHE_LINE_SIZE - 1));
  memory_chunk = p;
  start = (struct entry *)malloc(sizeof(struct entry));
  if (!start) {
    return -1;
  }
  start->next = start->prev = NULL;
  start->address = memory_chunk;
  start->size = max_bytes;
  start->available = 1;
  *root = (void*)start;
  return 0;
}

void ext_mpi_dmalloc_done(void *root) {
  struct entry *start = (struct entry *)root;
  struct entry *p;
  while (start) {
//    printf("heap list not empty %p %zu\n", start->address, start->size);
    p = start;
    start = start->next;
    free(p);
  }
}

void* ext_mpi_dmalloc(void *root, size_t numbytes) {
  struct entry *p = (struct entry *)root, *p2;
  numbytes = cache_alignment(numbytes);
  while (p && (!p->available || p->size < numbytes)) {
    p = p->next;
  }
  if (!p) return NULL;
  p->available = 0;
  if (p->size == numbytes) {
    return p->address;
  }
  p2 = (struct entry *)malloc(sizeof(struct entry));
  p2->address = p->address + numbytes;
  p2->available = 1;
  p2->size = p->size - numbytes;
  p2->next = p->next;
  p2->prev = p;
  p->size = numbytes;
  if (p->next) {
    p->next->prev = p2;
  }
  p->next = p2;
  return p->address;
}

void ext_mpi_dfree(void *root, void* ptr) {
  struct entry *p = (struct entry *)root, *p2;
  while (p && p->address != ptr) {
    p = p->next;
  }
  assert(p && !p->available);
  p->available = 1;
  if (p->next && p->next->available && p->address + p->size == p->next->address) {
    p->size += p->next->size;
    p2 = p->next;
    p->next = p->next->next;
    free(p2);
    if (p->next) {
      p->next->prev = p;
    }
  }
  p = p->prev;
  if (p && p->available) {
    if (p->address + p->size == p->next->address) {
      p->size += p->next->size;
      p2 = p->next;
      p->next = p->next->next;
      free(p2);
      if (p->next) {
        p->next->prev = p;
      }
    }
  }
}
