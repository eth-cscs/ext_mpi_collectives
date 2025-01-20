#include "byte_code.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef GPU_ENABLED
#include "gpu_core.h"
#include "gpu_shmem.h"
#endif

#ifdef GPU_ENABLED
struct mem_addresses {
  void *dest, *src;
  int size, start, reduce, number, dest_shadow, src_shadow;
  struct mem_addresses *next;
};

struct gpu_stream {
  int number;
  struct mem_addresses *mem_read_write;
  struct gpu_stream *next;
};

int gpu_byte_code_optimize(char *data, int type_size, int *jump) {
  int num_streams, index = 0, num_stream = 0, num_reductions = 0, i;
  long int max_size, size = 0, start;
  char *ldata, *p1, *p2, *data_temp, *ldata_temp[3], **p1_array, **p2_array;
  num_streams = *((int *)(data + sizeof(int)));
  max_size = *((long int *)(data + 2 * sizeof(int)));
  if (num_streams != 1) {
    return 0;
  }
  data_temp = malloc(10000);
  *((int *)data_temp) = *((int *)data);
  *((int *)(data_temp + sizeof(int))) = 3;
  *((long int *)(data_temp + 2 * sizeof(int))) = max_size;
  p1_array = (char**)malloc(1000 * sizeof(char*));
  p2_array = (char**)malloc(1000 * sizeof(char*));
  ldata = data + 2 * sizeof(int) + sizeof(long int) +
            (num_streams * index + num_stream) *
                (sizeof(char *) * 2 + sizeof(long int) * 2);
  p1 = *((char **)ldata);
  while (p1) {
    p2 = *((char **)(ldata + sizeof(char *)));
    size = *((long int *)(ldata + 2 * sizeof(char *)));
    start = *((long int *)(ldata + 2 * sizeof(char *) + sizeof(long int)));
    if (start) {
      free(p2_array);
      free(p1_array);
      free(data_temp);
      return 0;
    }
    if (num_reductions) {
      p1_array[num_reductions - 1] = p1;
      p2_array[num_reductions - 1] = p2;
    }
    num_reductions++;
    index++;
    ldata = data + 2 * sizeof(int) + sizeof(long int) +
            (num_streams * index + num_stream) *
                (sizeof(char *) * 2 + sizeof(long int) * 2);
    p1 = *((char **)ldata);
  }
  if (size / 128 < 3) {
    free(p2_array);
    free(p1_array);
    free(data_temp);
    return 0;
  }
  index = 0;
  ldata = data + 2 * sizeof(int) + sizeof(long int) +
            (num_streams * index + num_stream) *
                (sizeof(char *) * 2 + sizeof(long int) * 2);
  for (i = 0; i < 3; i++) {
    ldata_temp[i] = data_temp + 2 * sizeof(int) + sizeof(long int) +
              (3 * index + i) *
                  (sizeof(char *) * 2 + sizeof(long int) * 2);
  }
  p1 = *((char **)ldata);
  while (p1) {
    p2 = *((char **)(ldata + sizeof(char *)));
    size = *((long int *)(ldata + 2 * sizeof(char *)));
    start = *((long int *)(ldata + 2 * sizeof(char *) + sizeof(long int)));
    for (i = 0; i < 3; i++) {
      *((long int *)(ldata_temp[i] + 2 * sizeof(char *) + sizeof(long int))) = start;
    }
    if (index < 1) {
      for (i = 0; i < 3; i++) {
        *((char **)ldata_temp[i]) = p1 + (labs(size) / 128 * 128 / 3) * type_size * i;
        *((char **)(ldata_temp[i] + sizeof(char *))) = p2 + (labs(size) / 128 * 128 / 3) * type_size * i;
        if (i < 3 - 1) {
          *((long int *)(ldata_temp[i] + 2 * sizeof(char *))) = labs(size) / 128 * 128 / 3;
        } else {
          *((long int *)(ldata_temp[i] + 2 * sizeof(char *))) = labs(size) - (labs(size) / 128 * 128 / 3) * (3 - 1);
	}
	if (size < 0) {
	  *((long int *)(ldata_temp[i] + 2 * sizeof(char *))) *= -1;
        }
      }
    } else if (index < 4) {
      for (i = 0; i < 3; i++) {
        *((char **)ldata_temp[i]) = p1 + (labs(size) / 128 * 128 / 3) * type_size * i;
        *((char **)(ldata_temp[i] + sizeof(char *))) = p2_array[(index + i) % 3] + (labs(size) / 128 * 128 / 3) * type_size * i;
        if (i < 3 - 1) {
          *((long int *)(ldata_temp[i] + 2 * sizeof(char *))) = labs(size) / 128 * 128 / 3;
        } else {
          *((long int *)(ldata_temp[i] + 2 * sizeof(char *))) = labs(size) - (labs(size) / 128 * 128 / 3) * (3 - 1);
	}
	if (size < 0) {
	  *((long int *)(ldata_temp[i] + 2 * sizeof(char *))) *= -1;
        }
      }
    } else {
      for (i = 0; i < 3; i++) {
        *((char **)ldata_temp[i]) = p1_array[(index - 3 + i) % 3 + 3] + (labs(size) / 128 * 128 / 3) * type_size * i;
        *((char **)(ldata_temp[i] + sizeof(char *))) = p2 + (labs(size) / 128 * 128 / 3) * type_size * i;
        if (i < 3 - 1) {
          *((long int *)(ldata_temp[i] + 2 * sizeof(char *))) = labs(size) / 128 * 128 / 3;
        } else {
          *((long int *)(ldata_temp[i] + 2 * sizeof(char *))) = labs(size) - (labs(size) / 128 * 128 / 3) * (3 - 1);
	}
	if (size < 0) {
	  *((long int *)(ldata_temp[i] + 2 * sizeof(char *))) *= -1;
        }
      }
    }
    index++;
    ldata = data + 2 * sizeof(int) + sizeof(long int) +
            (num_streams * index + num_stream) *
                (sizeof(char *) * 2 + sizeof(long int) * 2);
    for (i = 0; i < 3; i++) {
      ldata_temp[i] = data_temp + 2 * sizeof(int) + sizeof(long int) +
              (3 * index + i) *
                  (sizeof(char *) * 2 + sizeof(long int) * 2);
    }
    p1 = *((char **)ldata);
  }
  for (i = 0; i < 3; i++) {
    *((char **)ldata_temp[i]) = 0;
  }
  memcpy(data, data_temp, 2 * sizeof(int) + sizeof(long int) + (3 * index + 3 - 1) * (sizeof(char *) * 2 + sizeof(long int) * 2));
  *jump = 2 * sizeof(int) + sizeof(long int) + (3 * index + 3 - 1) * (sizeof(char *) * 2 + sizeof(long int) * 2);
  free(p2_array);
  free(p1_array);
  free(data_temp);
  return 1;
}

static int gpu_add_to_mem_addresses_range(struct mem_addresses **list,
                                          void *dest, void *src, int size,
                                          int reduce, int number) {
  struct mem_addresses *p, *p2;
  p = (struct mem_addresses *)malloc(sizeof(struct mem_addresses));
  if (!p)
    goto error;
  p->dest = dest;
  p->src = src;
  p->size = size;
  p->reduce = reduce;
  p->number = number;
  p->start = 0;
  p->dest_shadow = p->src_shadow = 0;
  if (!(*list)) {
    p->next = *list;
    *list = p;
  } else {
    p->next = NULL;
    p2 = *list;
    while (p2->next) {
      p2 = p2->next;
    }
    p2->next = p;
  }
  return 0;
error:
  return ERROR_MALLOC;
}

static void gpu_delete_mem_addresses_range(struct mem_addresses **list) {
  struct mem_addresses *p;
  while (*list) {
    p = *list;
    *list = p->next;
    free(p);
  }
}

static void gpu_merge_streams(struct gpu_stream *stream1, struct gpu_stream *stream2) {
  struct gpu_stream *stream3;
  struct mem_addresses *p1, *p2, *p3;
  p1 = stream1->mem_read_write;
  p2 = stream2->mem_read_write;
  while (p2) {
    while (p1->next && p1->next->number < p2->number) {
      p1 = p1->next;
    }
    p3 = p2->next;
    if (p1->next) {
      p2->next = p1->next;
      p1->next = p2;
    } else {
      p1->next = p2;
      p2->next = NULL;
    }
    p2 = p3;
  }
  stream3 = stream1;
  while (stream3->next != stream2) {
    stream3 = stream3->next;
  }
  stream3->next = stream3->next->next;
  free(stream2);
}

static int gpu_is_in_mem_addresses_range(struct mem_addresses *list, void *dest,
                                         void *src, int size) {
  while (list) {
    if (((((char *)dest + size) > (char *)list->src) &&
            ((char *)dest < ((char *)list->src + list->size))) ||
        ((((char *)dest + size) > (char *)list->dest) &&
            ((char *)dest < ((char *)list->dest + list->size))) ||
        ((((char *)src + size) > (char *)list->dest) &&
            ((char *)src < ((char *)list->dest + list->size))) ||
        ((((char *)src + size) > (char *)list->src) &&
            ((char *)src < ((char *)list->src + list->size)))) {
      return 1;
    } else {
      list = list->next;
    }
  }
  return 0;
}

static int gpu_shift_memory_blocks(struct gpu_stream *streams) {
  struct mem_addresses *p1, *p2;
  int ret = 0, i;
  while (streams) {
    p1 = streams->mem_read_write;
    while (p1) {
      p2 = p1->next;
      while (p2) {
	if (p1->dest + p1->dest_shadow < p2->src + p2->src_shadow && p1->dest + p1->size > p2->src + p2->src_shadow) {
          i = p2->src + p2->src_shadow - p1->dest - p1->dest_shadow;
	  p2->start += i;
	  p2->size += i;
	  p2->src -= i;
	  p2->dest -= i;
//	  p2->dest_shadow += i;
          ret = 1;
	  if (p2->size < 0) return -1;
	}
	if (p2->src + p2->src_shadow < p1->dest + p1->dest_shadow && p2->src + p2->size > p1->dest + p1->dest_shadow) {
          i = p1->dest + p1->dest_shadow - p2->src - p2->src_shadow;
	  p1->start += i;
	  p1->size += i;
	  p1->src -= i;
	  p1->dest -= i;
//	  p1->src_shadow += i;
          ret = 1;
	  if (p1->size < 0) return -1;
	}
	if (p1->src + p1->src_shadow < p2->dest + p2->dest_shadow && p1->src + p1->size > p2->dest + p2->dest_shadow) {
          i = p2->dest + p2->dest_shadow - p1->src - p1->src_shadow;
	  p2->start += i;
	  p2->size += i;
	  p2->src -= i;
	  p2->dest -= i;
//	  p2->src_shadow += i;
          ret = 1;
	  if (p2->size < 0) return -1;
	}
	if (p2->dest + p2->dest_shadow < p1->src + p1->src_shadow && p2->dest + p2->size > p1->src + p1->dest_shadow) {
          i = p1->src + p1->src_shadow - p2->dest - p2->dest_shadow;
	  p1->start += i;
	  p1->size += i;
	  p1->src -= i;
	  p1->dest -= i;
//	  p1->dest_shadow += i;
          ret = 1;
	  if (p1->size < 0) return -1;
	}
        p2 = p2->next;
      }
      p1 = p1->next;
    }
    if (streams) streams = streams->next;
  }
  return ret;
}

static int gpu_byte_code_arrange(struct gpu_stream *streams) {
  struct gpu_stream *stream1, *stream2;
  struct mem_addresses *p;
  int flag = 1, ret;
  while (flag) {
    flag = 0;
    stream1 = streams;
    while (stream1) {
      stream2 = stream1->next;
      while (stream2) {
	p = stream2->mem_read_write;
	while (p) {
	  if (gpu_is_in_mem_addresses_range(stream1->mem_read_write, p->dest, p->src, p->size)) {
	    gpu_merge_streams(stream1, stream2);
	    p = NULL;
	    stream1 = stream2 = NULL;
	    flag = 1;
	  } else {
	    p = p->next;
	  }
	}
	if (stream2) stream2 = stream2->next;
      }
      if (stream1) stream1 = stream1->next;
    }
    flag |= (ret = gpu_shift_memory_blocks(streams));
    if (ret < 0) return -1;
  }
  return 0;
}

static int gpu_add_stream_and_get_stream_number(struct gpu_stream **streams,
                                                void *dest, void *src,
                                                int count, int reduce, int number) {
  struct gpu_stream *lstreams, *lstreamst;
  int stream_number;
  stream_number = number;
  lstreams = (struct gpu_stream *)malloc(sizeof(struct gpu_stream));
  if (!lstreams)
    return ERROR_MALLOC;
  lstreams->number = stream_number;
  lstreams->mem_read_write = NULL;
  gpu_add_to_mem_addresses_range(&lstreams->mem_read_write, dest, src, count,
                                 reduce, number);
  if (!(*streams)) {
    lstreams->next = *streams;
    *streams = lstreams;
  } else {
    lstreams->next = NULL;
    lstreamst = *streams;
    while (lstreamst->next) {
      lstreamst = lstreamst->next;
    }
    lstreamst->next = lstreams;
  }
  return stream_number;
}

static void gpu_delete_streams(struct gpu_stream **streams) {
  struct gpu_stream *lstreams;
  lstreams = *streams;
  while (lstreams) {
    gpu_delete_mem_addresses_range(&lstreams->mem_read_write);
    lstreams = lstreams->next;
  }
  *streams = NULL;
}

static int gpu_get_num_streams(struct gpu_stream *streams) {
  int number = 0;
  while (streams) {
    number++;
    streams = streams->next;
  }
  return number;
}

static int gpu_get_num_entries(struct gpu_stream *streams) {
  struct mem_addresses *p;
  int number = 0, lnumber;
  while (streams) {
    lnumber = 0;
    p = streams->mem_read_write;
    while (p) {
      lnumber++;
      p = p->next;
    }
    if (lnumber > number) {
      number = lnumber;
    }
    streams = streams->next;
  }
  return number;
}

static int gpu_get_entry(struct gpu_stream *streams, int stream, int entry,
                         void **dest, void **src, int *count, int *reduce, int *start) {
  struct mem_addresses *p;
  int i;
  for (i = 0; (i < stream) && streams; i++) {
    streams = streams->next;
  }
  if (!streams)
    return 0;
  p = streams->mem_read_write;
  for (i = 0; (i < entry) && p; i++) {
    p = p->next;
  }
  if (!p)
    return 0;
  *dest = p->dest;
  *src = p->src;
  *count = p->size;
  *reduce = p->reduce;
  *start = p->start;
  return 1;
}

static int gpu_byte_code_flush1(struct gpu_stream *streams,
                                 char *gpu_byte_code, int gpu_byte_code_counter,
                                 int type_size) {
  long int lcount, max_size;
  int num_entries, num_streams, stream, entry, count, reduce, start;
  void *dest, *src;
  if (gpu_byte_code_arrange(streams) < 0) return -1;
  if (gpu_byte_code) {
    gpu_byte_code += gpu_byte_code_counter;
    num_streams = ((int *)gpu_byte_code)[1] = gpu_get_num_streams(streams);
    num_entries = ((int *)gpu_byte_code)[0] = gpu_get_num_entries(streams);
    max_size = 0;
    for (entry = 0; entry < num_entries; entry++) {
      for (stream = 0; stream < num_streams; stream++) {
        if (gpu_get_entry(streams, stream, entry, &dest, &src, &count,
                          &reduce, &start)) {
          count /= type_size;
          lcount = count;
          if (reduce) {
            lcount = -lcount;
          }
          ((void **)(gpu_byte_code + 2 * sizeof(int) + sizeof(long int) +
                     (entry * num_streams + stream) *
                         (2 * sizeof(void *) + 2 * sizeof(long int))))[0] = dest;
          ((void **)(gpu_byte_code + 2 * sizeof(int) + sizeof(long int) +
                     (entry * num_streams + stream) *
                         (2 * sizeof(void *) + 2 * sizeof(long int))))[1] = src;
          ((long int *)(gpu_byte_code + 2 * sizeof(int) + sizeof(long int) +
                        2 * sizeof(void *) +
                        (entry * num_streams + stream) *
                            (2 * sizeof(void *) + 2 * sizeof(long int))))[0] =
              lcount;
          ((long int *)(gpu_byte_code + 2 * sizeof(int) + sizeof(long int) +
                        2 * sizeof(void *) +
                        (entry * num_streams + stream) *
                            (2 * sizeof(void *) + 2 * sizeof(long int))))[1] =
              start / type_size;
          if (labs(lcount) > max_size) {
            max_size = labs(lcount);
          }
        }
      }
    }
    if (num_entries) {
      ((long int *)gpu_byte_code)[1] = max_size;
    }
  }
  return 0;
}

static void gpu_byte_code_flush2(struct gpu_stream **streams,
                                 int *gpu_byte_code_counter) {
  int num_entries, num_streams, jump;
  num_streams = gpu_get_num_streams(*streams);
  num_entries = gpu_get_num_entries(*streams);
  if (num_entries) {
    jump = 2 * sizeof(int) + sizeof(long int) +
           (num_entries + 1) * num_streams *
               (2 * sizeof(char *) + 2 * sizeof(long int));
    *gpu_byte_code_counter += jump;
  }
  gpu_delete_streams(streams);
}

static int gpu_byte_code_add(struct gpu_stream **streams, void *dest, void *src,
                             int count, int reduce, int number, int fallback) {
  int num_stream;
  if (fallback && number > 1) return -1;
  num_stream =
      gpu_add_stream_and_get_stream_number(streams, dest, src, count, reduce, number);
  if (num_stream < 0) {
    return -1;
  }
  return 0;
}

static int flush_complete(char **ip, struct gpu_stream **streams,
                           char *header_gpu_byte_code, char *gpu_byte_code,
                           int *gpu_byte_code_counter, int reduction_op,
                           int isdryrun) {
  int type_size = 1, ret, jump;
  code_put_char(ip, OPCODE_GPUKERNEL, isdryrun);
  code_put_char(ip, reduction_op, isdryrun);
  type_size = get_type_size(reduction_op);
  code_put_pointer(ip, header_gpu_byte_code + *gpu_byte_code_counter, isdryrun);
  ret = gpu_byte_code_flush1(*streams, gpu_byte_code, *gpu_byte_code_counter, type_size);
  if (ret < 0) {
    gpu_delete_streams(streams);
    return ret;
  }
  if (!isdryrun) {
    code_put_int(ip,
                 ((long int *)(gpu_byte_code + *gpu_byte_code_counter))[1] *
                     ((int *)(gpu_byte_code + *gpu_byte_code_counter))[1] * type_size,
                 isdryrun);
  } else {
    code_put_int(ip, 0, isdryrun);
  }
  jump = *gpu_byte_code_counter;
  gpu_byte_code_flush2(streams, gpu_byte_code_counter);
  jump = *gpu_byte_code_counter - jump;
  if (gpu_byte_code) {
    *gpu_byte_code_counter -= jump;
//    gpu_byte_code_optimize(gpu_byte_code + *gpu_byte_code_counter, type_size, &jump);
    *gpu_byte_code_counter += jump;
  } else {
//    *gpu_byte_code_counter = *gpu_byte_code_counter * 4 + 1000;
  }
  return ret;
}
#endif

int ext_mpi_generate_byte_code(char **shmem,
                               int *shmemid, int *shmem_sizes,
                               char *buffer_in, char **sendbufs, char **recvbufs,
                               int barriers_size, char *locmem,
                               int reduction_op, void *func, int *global_ranks,
                               char *code_out, int size_request, int *ranks_node,
                               int node_num_cores_row, int node_num_cores_column,
                               int *shmemid_gpu, char **shmem_gpu, int *gpu_byte_code_counter, int gpu_fallback, int tag) {
  struct line_memcpy_reduce data_memcpy_reduce;
  struct line_irecv_isend data_irecv_isend;
  char line[1000], *ip = code_out;
  enum eassembler_type estring1a, estring1, estring2;
  int integer1, integer2, integer3, integer4, isdryrun = (code_out == NULL),
      ascii, num_cores, socket_rank, num_sockets_per_node, num_cores_socket_barrier, num_cores_socket_barrier_small, i, j;
  struct header_byte_code header_temp;
  struct header_byte_code *header;
#ifdef GPU_ENABLED
  char *gpu_byte_code = NULL;
  int on_gpu, reduce, added = 0, vector_length, memopt_number = 1;
  struct gpu_stream *streams = NULL;
  void *p1, *p2;
#endif
  struct parameters_block *parameters;
  buffer_in += ext_mpi_read_parameters(buffer_in, &parameters);
  ascii = parameters->ascii_in;
#ifdef GPU_ENABLED
  on_gpu = parameters->on_gpu;
  vector_length = parameters->counts[0];
#endif
  num_cores = parameters->socket_row_size*parameters->socket_column_size;
  num_cores_socket_barrier = parameters->socket_size_barrier;
  num_cores_socket_barrier_small = parameters->socket_size_barrier_small;
  socket_rank = parameters->socket_rank;
  num_sockets_per_node = parameters->num_sockets_per_node;
  ext_mpi_delete_parameters(parameters);
  memset(&header_temp, 0, sizeof(struct header_byte_code));
  if (isdryrun) {
    header = &header_temp;
    header->mpi_user_function = func;
    header->num_cores = num_cores;
    header->socket_rank = socket_rank;
    header->num_sockets_per_node = num_sockets_per_node;
    header->function = NULL;
    header->num_cores_socket_barrier = num_cores_socket_barrier;
    header->num_cores_socket_barrier_small = num_cores_socket_barrier_small;
  } else {
    memset(ip, 0, sizeof(struct header_byte_code));
    header = (struct header_byte_code *)ip;
    header->mpi_user_function = func;
    header->barrier_counter_socket = 0;
    header->barrier_counter_node = 0;
    header->num_cores_socket_barrier = num_cores_socket_barrier;
    header->num_cores_socket_barrier_small = num_cores_socket_barrier_small;
    if (shmem) {
      header->barrier_shmem_node = (char **)malloc(num_cores * num_sockets_per_node * sizeof(char *));
      header->barrier_shmem_socket = (char **)malloc(num_cores * sizeof(char *));
      header->barrier_shmem_socket_small = (char **)malloc(num_cores * sizeof(char *));
      for (i = 0; i < num_cores * num_sockets_per_node; i++) {
        header->barrier_shmem_node[i] = shmem[i] + shmem_sizes[i] - barriers_size;
      }
      for (i = 0; i < num_cores_socket_barrier; i++) {
	if (socket_rank >= num_cores_socket_barrier) {
	  j = 0;
	} else {
	  j = i;
	  if (j + socket_rank >= num_cores_socket_barrier) {
	    j = (j + num_cores - num_cores_socket_barrier) % num_cores;
	  }
	}
        header->barrier_shmem_socket[i] = shmem[j] + shmem_sizes[j] - 2 * barriers_size;
      }
      for (i = 0; i < num_cores_socket_barrier_small; i++) {
	if (socket_rank >= num_cores_socket_barrier_small) {
	  j = 0;
	} else {
	  j = i;
	  if (j + socket_rank >= num_cores_socket_barrier_small) {
	    j = (j + num_cores - num_cores_socket_barrier_small) % num_cores;
	  }
	}
        header->barrier_shmem_socket_small[i] = shmem[j] + shmem_sizes[j] - 2 * barriers_size;
      }
    }
    header->shmem = shmem;
    header->shmemid = shmemid;
    header->shmem_sizes = shmem_sizes;
    header->locmem = locmem;
    header->sendbufs = sendbufs;
    header->recvbufs = recvbufs;
#ifdef GPU_ENABLED
    header->shmem_gpu = shmem_gpu;
    header->shmemid_gpu = shmemid_gpu;
    if (on_gpu) {
      shmem = header->shmem_gpu;
    }
#endif
    header->node_num_cores_row = node_num_cores_row;
    header->node_num_cores_column = node_num_cores_column;
    header->num_cores = num_cores;
    header->socket_rank = socket_rank;
    header->num_sockets_per_node = num_sockets_per_node;
    header->tag = tag;
#ifdef GPU_ENABLED
    header->gpu_byte_code = NULL;
#endif
    header->function = NULL;
  }
#ifdef GPU_ENABLED
  if (on_gpu) {
    if (!isdryrun) {
      gpu_byte_code = (char *)malloc(*gpu_byte_code_counter);
      if (!gpu_byte_code)
        goto error;
      memset(gpu_byte_code, 0, *gpu_byte_code_counter);
      if (recvbufs && recvbufs[0] && ((unsigned long int)recvbufs[0] & 0xF000000000000000) != RECV_PTR_GPU) {
        ext_mpi_gpu_malloc((void **)&header->gpu_byte_code, *gpu_byte_code_counter);
      } else {
	header->gpu_byte_code = malloc(*gpu_byte_code_counter);
	if (!header->gpu_byte_code)
          goto error;
      }
      *gpu_byte_code_counter = 0;
    } else {
      gpu_byte_code = NULL;
    }
  }
#endif
  if (ranks_node) {
    header->ranks_node = (int*)malloc(node_num_cores_row * num_sockets_per_node * sizeof(int));
    for (i = 0; i < node_num_cores_row * num_sockets_per_node; i++) {
      header->ranks_node[i] = ranks_node[i];
    }
  }
  ip += sizeof(struct header_byte_code);
  while ((integer1 = ext_mpi_read_line(buffer_in, line, ascii)) > 0) {
    buffer_in += integer1;
    ext_mpi_read_assembler_line(line, 0, "sd", &estring1, &integer1);
#ifdef NCCL_ENABLED
    if (estring1 == estart) {
      code_put_char(&ip, OPCODE_START, isdryrun);
    }
#endif
    if (estring1 == ereturn) {
#ifdef GPU_ENABLED
      if (on_gpu) {
        if (added) {
          if (flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code, gpu_byte_code_counter, reduction_op, isdryrun) < 0) goto failed;
          added = 0;
	  memopt_number = 1;
        }
      }
#endif
      code_put_char(&ip, OPCODE_RETURN, isdryrun);
    }
    if (estring1 == eset_node_barrier) {
      if (header->num_cores != 1 || num_sockets_per_node != 1) {
#ifdef GPU_ENABLED
        if (on_gpu) {
          if (added) {
            if (flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code, gpu_byte_code_counter, reduction_op, isdryrun) < 0) goto failed;
            added = 0;
	    memopt_number = 1;
          }
          code_put_char(&ip, OPCODE_GPUSYNCHRONIZE, isdryrun);
        }
#endif
        code_put_char(&ip, OPCODE_NODEBARRIER_ATOMIC_SET, isdryrun);
	if (header->barrier_shmem_node) {
          code_put_pointer(&ip, header->barrier_shmem_node[0], isdryrun);
	} else {
          code_put_pointer(&ip, NULL, isdryrun);
	}
      }
    }
    if (estring1 == ewait_node_barrier) {
      if (header->num_cores != 1 || num_sockets_per_node != 1) {
        code_put_char(&ip, OPCODE_NODEBARRIER_ATOMIC_WAIT, isdryrun);
	if (header->barrier_shmem_node) {
	  if (recvbufs && !((unsigned long int)recvbufs[0] & 0xF000000000000000)) {
            code_put_pointer(&ip, header->barrier_shmem_node[integer1], isdryrun);
	  } else {
            code_put_pointer(&ip, (void*)(NULL + integer1), isdryrun);
	  }
	} else {
          code_put_pointer(&ip, NULL, isdryrun);
	}
      }
    }
    if (estring1 == ewaitall) {
#ifdef GPU_ENABLED
      if (on_gpu) {
        if (added) {
          if (flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code, gpu_byte_code_counter, reduction_op, isdryrun) < 0) goto failed;
          added = 0;
	  memopt_number = 1;
        }
      }
#endif
      if (integer1) {
        code_put_char(&ip, OPCODE_MPIWAITALL, isdryrun);
        code_put_int(&ip, integer1, isdryrun);
        code_put_pointer(&ip, header->locmem, isdryrun);
      }
#ifdef GPU_ENABLED
      if (on_gpu) {
        code_put_char(&ip, OPCODE_GPUSYNCHRONIZE, isdryrun);
      }
#endif
    }
    if (estring1 == ewaitany) {
      code_put_char(&ip, OPCODE_MPIWAITANY, isdryrun);
      ext_mpi_read_assembler_line(line, 0, "sddsd", &estring1, &integer1,
                                  &integer2, &estring2, &integer3);
      code_put_int(&ip, integer1, isdryrun);
      code_put_int(&ip, integer2, isdryrun);
      code_put_pointer(&ip, header->locmem, isdryrun);
    }
    if (estring1 == eattached) {
      code_put_char(&ip, OPCODE_ATTACHED, isdryrun);
    }
    if ((estring1 == eisend) || (estring1 == eirecv)) {
#ifdef GPU_ENABLED
      if (on_gpu) {
        if (added) {
          if (flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code, gpu_byte_code_counter, reduction_op, isdryrun) < 0) goto failed;
          added = 0;
	  memopt_number = 1;
        }
      }
#endif
      ext_mpi_read_irecv_isend(line, &data_irecv_isend);
      estring1 = data_irecv_isend.type;
      estring2 = data_irecv_isend.buffer_type;
      integer1 = data_irecv_isend.offset;
      integer2 = data_irecv_isend.size;
      integer3 = data_irecv_isend.partner;
      integer4 = data_irecv_isend.tag;
      if (estring1 == eisend) {
#ifdef GPU_ENABLED
        if (on_gpu) {
          code_put_char(&ip, OPCODE_GPUSYNCHRONIZE, isdryrun);
        }
#endif
        code_put_char(&ip, OPCODE_MPIISEND, isdryrun);
      } else {
        code_put_char(&ip, OPCODE_MPIIRECV, isdryrun);
      }
      if (estring2 == esendbufp) {
	if (sendbufs) {
          code_put_pointer(&ip, (void *)(sendbufs[data_irecv_isend.buffer_number] + integer1), isdryrun);
	} else {
          code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	}
      } else {
        if (estring2 == erecvbufp) {
	  if (recvbufs) {
            code_put_pointer(&ip, (void *)(recvbufs[data_irecv_isend.buffer_number] + integer1), isdryrun);
	  } else {
            code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	  }
        } else {
	  if (shmem) {
            code_put_pointer(&ip, (void *)(shmem[data_irecv_isend.buffer_number] + integer1), isdryrun);
	  } else {
            code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	  }
        }
      }
      code_put_int(&ip, integer2, isdryrun);
      if (global_ranks) {
        code_put_int(&ip, global_ranks[integer3], isdryrun);
      } else {
        code_put_int(&ip, -1, isdryrun);
      }
      code_put_pointer(&ip, header->locmem + size_request * integer4, isdryrun);
    }
    if (estring1 == enode_barrier) {
      code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);
    }
    if (estring1 == ememory_fence) {
      code_put_char(&ip, OPCODE_MEMORY_FENCE, isdryrun);
    }
    if (estring1 == ememory_fence_store) {
      code_put_char(&ip, OPCODE_MEMORY_FENCE_STORE, isdryrun);
    }
    if (estring1 == ememory_fence_load) {
      code_put_char(&ip, OPCODE_MEMORY_FENCE_LOAD, isdryrun);
    }
    if (estring1 == esocket_barrier) {
      if (header->num_cores != 1) {
#ifdef GPU_ENABLED
        if (on_gpu) {
          if (added) {
            if (flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code, gpu_byte_code_counter, reduction_op, isdryrun) < 0) goto failed;
            added = 0;
	    memopt_number = 1;
          }
          code_put_char(&ip, OPCODE_GPUSYNCHRONIZE, isdryrun);
        }
#endif
        code_put_char(&ip, OPCODE_SOCKETBARRIER, isdryrun);
      }
    }
    if (estring1 == esocket_barrier_small) {
      if (header->num_cores != 1) {
        code_put_char(&ip, OPCODE_SOCKETBSMALL, isdryrun);
      }
    }
#ifdef GPU_ENABLED
    if (estring1 == egemv) {
      ext_mpi_read_assembler_line(line, 0, "sdd", &estring1, &integer1, &integer2);
      code_put_char(&ip, OPCODE_GPUGEMV, isdryrun);
      code_put_char(&ip, reduction_op, isdryrun);
      if (shmem) {
        code_put_pointer(&ip, (void *)(shmem[0]), isdryrun);
      } else {
        code_put_pointer(&ip, (void *)(NULL), isdryrun);
      }
      code_put_int(&ip, integer1, isdryrun);
      code_put_int(&ip, integer2, isdryrun);
    }
#endif
    if ((estring1 == ememcpy) || (estring1 == ereduce) || (estring1 == einvreduce) ||
        (estring1 == esreduce) || (estring1 == esinvreduce) || (estring1 == esmemcpy)) {
      ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce);
      estring1 = data_memcpy_reduce.type;
      estring1a = data_memcpy_reduce.buffer_type1;
      integer1 = data_memcpy_reduce.offset1;
      estring2 = data_memcpy_reduce.buffer_type2;
      integer2 = data_memcpy_reduce.offset2;
      integer3 = data_memcpy_reduce.size;
#ifdef GPU_ENABLED
      if (!on_gpu) {
#endif
        if ((estring1 == ememcpy) || (estring1 == esmemcpy)) {
          code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
        } else {
	  if (estring1 == ereduce || estring1 == esreduce) {
            code_put_char(&ip, OPCODE_REDUCE, isdryrun);
	  } else {
            code_put_char(&ip, OPCODE_INVREDUCE, isdryrun);
	  }
          code_put_char(&ip, reduction_op, isdryrun);
          integer3 /= get_type_size(reduction_op);
        }
        if (estring1a == esendbufp) {
	  if (sendbufs) {
            code_put_pointer(&ip, (void *)(sendbufs[data_memcpy_reduce.buffer_number1] + integer1), isdryrun);
	  } else {
            code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	  }
        } else {
          if (estring1a == erecvbufp) {
	    if (recvbufs) {
              code_put_pointer(&ip, (void *)(recvbufs[data_memcpy_reduce.buffer_number1] + integer1), isdryrun);
	    } else {
              code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	    }
          } else {
            if (shmem) {
              code_put_pointer(&ip, (void *)(shmem[data_memcpy_reduce.buffer_number1] + integer1), isdryrun);
	    } else {
              code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	    }
          }
        }
        if (estring2 == esendbufp) {
	  if (sendbufs) {
            code_put_pointer(&ip, (void *)(sendbufs[data_memcpy_reduce.buffer_number2] + integer2), isdryrun);
	  } else {
            code_put_pointer(&ip, (void *)(NULL + integer2), isdryrun);
	  }
        } else {
          if (estring2 == erecvbufp) {
	    if (recvbufs) {
              code_put_pointer(&ip, (void *)(recvbufs[data_memcpy_reduce.buffer_number2] + integer2), isdryrun);
	    } else {
              code_put_pointer(&ip, (void *)(NULL + integer2), isdryrun);
	    }
          } else {
            if (shmem) {
              code_put_pointer(&ip, (void *)(shmem[data_memcpy_reduce.buffer_number2] + integer2), isdryrun);
	    } else {
              code_put_pointer(&ip, (void *)(NULL + integer2), isdryrun);
	    }
          }
        }
        code_put_int(&ip, integer3, isdryrun);
#ifdef GPU_ENABLED
      } else {
        if ((estring1 == ememcpy) || (estring1 == esmemcpy)) {
          reduce = 0;
        } else {
          reduce = 1;
        }
        if (estring1a == esendbufp) {
	  if (sendbufs) {
	    p1 = sendbufs[data_memcpy_reduce.buffer_number1] + integer1;
	  } else {
	    p1 = (void *)(NULL + integer1);
	  }
        } else if (estring1a == erecvbufp) {
	  if (recvbufs) {
	    p1 = recvbufs[data_memcpy_reduce.buffer_number1] + integer1;
	  } else {
	    p1 = (void *)(NULL + integer1);
	  }
        } else if (shmem) {
	  p1 = (void *)(shmem[data_memcpy_reduce.buffer_number1] + integer1);
	} else {
	  p1 = (void *)(NULL + integer1);
        }
        if (estring2 == esendbufp) {
	  if (sendbufs) {
	    p2 = sendbufs[data_memcpy_reduce.buffer_number2] + integer2;
          } else {
	    p2 = (void *)(NULL + integer2);
	  }
        } else if (estring2 == erecvbufp) {
	  if (recvbufs) {
	    p2 = recvbufs[data_memcpy_reduce.buffer_number2] + integer2;
          } else {
	    p2 = (void *)(NULL + integer2);
	  }
        } else if (shmem) {
          p2 = (void *)(shmem[data_memcpy_reduce.buffer_number2] + integer2);
	} else {
          p2 = (void *)(NULL + integer2);
        }
        if (gpu_byte_code_add(&streams, p1, p2, integer3, reduce, memopt_number++, gpu_fallback) < 0) {
          if (flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code, gpu_byte_code_counter, reduction_op, isdryrun) < 0) goto failed;
	  memopt_number = 1;
	  gpu_byte_code_add(&streams, p1, p2, integer3, reduce, memopt_number++, gpu_fallback);
        }
        added = 1;
      }
#endif
    }
  }
#ifdef GPU_ENABLED
  if (!isdryrun && on_gpu) {
    if (recvbufs && recvbufs[0] && ((unsigned long int)recvbufs[0] & 0xF000000000000000) != RECV_PTR_GPU) {
      ext_mpi_gpu_memcpy_hd(header->gpu_byte_code, gpu_byte_code, *gpu_byte_code_counter);
    } else {
      memcpy(header->gpu_byte_code, gpu_byte_code, *gpu_byte_code_counter);
    }
//    ext_mpi_gemv_init(reduction_op, vector_length, num_cores, &header->gpu_gemv_var);
  } else {
    header->gpu_gemv_var.handle = 0;
  }
  header->gpu_byte_code_size = *gpu_byte_code_counter;
  free(gpu_byte_code);
#endif
  header->size_to_return = ip - code_out;
  return (ip - code_out);
#ifdef GPU_ENABLED
failed:
  free(gpu_byte_code);
  return -7;
error:
  return ERROR_MALLOC;
#endif
}
