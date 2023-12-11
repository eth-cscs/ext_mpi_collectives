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
  int size, reduce;
  struct mem_addresses *next;
};

static int gpu_add_to_mem_addresses_range(struct mem_addresses **list,
                                          void *dest, void *src, int size,
                                          int reduce) {
  struct mem_addresses *p, *p2;
  p = (struct mem_addresses *)malloc(sizeof(struct mem_addresses));
  if (!p)
    goto error;
  p->dest = dest;
  p->src = src;
  p->size = size;
  p->reduce = reduce;
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

static int gpu_is_in_mem_addresses_range(struct mem_addresses *list, void *dest,
                                         void *src, int size) {
  while (list) {
    if (((((char *)dest + size) > (char *)list->src) &&
            ((char *)dest < ((char *)list->src + list->size))) ||
        ((((char *)dest + size) > (char *)list->dest) &&
            ((char *)dest < ((char *)list->dest + list->size))) ||
        ((((char *)src + size) > (char *)list->dest) &&
            ((char *)src < ((char *)list->dest + list->size)))) {
      return 1;
    } else {
      list = list->next;
    }
  }
  return 0;
}

struct gpu_stream {
  int number;
  struct mem_addresses *mem_read_write;
  struct gpu_stream *next;
};

static int gpu_add_stream_and_get_stream_number(struct gpu_stream **streams,
                                                void *dest, void *src,
                                                int count, int reduce) {
  struct gpu_stream *lstreams, *lstreamst;
  int number = -1;
  lstreams = *streams;
  while (lstreams) {
    if (gpu_is_in_mem_addresses_range(lstreams->mem_read_write, dest, src,
                                      count)) {
      number = lstreams->number;
      lstreamst = lstreams->next;
      while (lstreamst) {
        if (gpu_is_in_mem_addresses_range(lstreamst->mem_read_write, dest, src,
                                          count)) {
          return -1;
        }
        lstreamst = lstreamst->next;
      }
      gpu_add_to_mem_addresses_range(&lstreams->mem_read_write, dest, src,
                                     count, reduce);
      return number;
    }
    if (lstreams->number > number) {
      number = lstreams->number;
    }
    lstreams = lstreams->next;
  }
  number++;
  lstreams = (struct gpu_stream *)malloc(sizeof(struct gpu_stream));
  if (!lstreams)
    return ERROR_MALLOC;
  lstreams->number = number;
  lstreams->mem_read_write = NULL;
  gpu_add_to_mem_addresses_range(&lstreams->mem_read_write, dest, src, count,
                                 reduce);
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
  return number;
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
                         void **dest, void **src, int *count, int *reduce) {
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
  return 1;
}

static void gpu_byte_code_flush1(struct gpu_stream *streams,
                                 char *gpu_byte_code, int gpu_byte_code_counter,
                                 int type_size) {
  long int lcount, max_size;
  int num_entries, num_streams, stream, entry, count, reduce;
  void *dest, *src;
  if (gpu_byte_code) {
    gpu_byte_code += gpu_byte_code_counter;
    num_streams = ((int *)gpu_byte_code)[1] = gpu_get_num_streams(streams);
    num_entries = ((int *)gpu_byte_code)[0] = gpu_get_num_entries(streams);
    max_size = 0;
    for (entry = 0; entry < num_entries; entry++) {
      for (stream = 0; stream < num_streams; stream++) {
        if (gpu_get_entry(streams, stream, entry, &dest, &src, &count,
                          &reduce)) {
          count /= type_size;
          lcount = count;
          if (reduce) {
            lcount = -lcount;
          }
          ((void **)(gpu_byte_code + 2 * sizeof(int) + sizeof(long int) +
                     (entry * num_streams + stream) *
                         (2 * sizeof(void *) + sizeof(long int))))[0] = dest;
          ((void **)(gpu_byte_code + 2 * sizeof(int) + sizeof(long int) +
                     (entry * num_streams + stream) *
                         (2 * sizeof(void *) + sizeof(long int))))[1] = src;
          ((long int *)(gpu_byte_code + 2 * sizeof(int) + sizeof(long int) +
                        2 * sizeof(void *) +
                        (entry * num_streams + stream) *
                            (2 * sizeof(void *) + sizeof(long int))))[0] =
              lcount;
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
}

static void gpu_byte_code_flush2(struct gpu_stream **streams,
                                 int *gpu_byte_code_counter) {
  int num_entries, num_streams, jump;
  num_streams = gpu_get_num_streams(*streams);
  num_entries = gpu_get_num_entries(*streams);
  if (num_entries) {
    jump = 2 * sizeof(int) + sizeof(long int) +
           (num_entries + 1) * num_streams *
               (2 * sizeof(char *) + sizeof(long int));
    *gpu_byte_code_counter += jump;
  }
  gpu_delete_streams(streams);
}

static int gpu_byte_code_add(struct gpu_stream **streams, void *dest, void *src,
                             int count, int reduce) {
  int num_stream;
  num_stream =
      gpu_add_stream_and_get_stream_number(streams, dest, src, count, reduce);
  if (num_stream < 0) {
    return -1;
  }
  return 0;
}

static void flush_complete(char **ip, struct gpu_stream **streams,
                           char *header_gpu_byte_code, char *gpu_byte_code,
                           int *gpu_byte_code_counter, int reduction_op,
                           int isdryrun) {
  int type_size = 1;
  code_put_char(ip, OPCODE_GPUKERNEL, isdryrun);
  code_put_char(ip, reduction_op, isdryrun);
  switch (reduction_op) {
  case OPCODE_REDUCE_SUM_INT:
    type_size = sizeof(int);
    break;
  case OPCODE_REDUCE_SUM_FLOAT:
    type_size = sizeof(float);
    break;
  case OPCODE_REDUCE_SUM_LONG_INT:
    type_size = sizeof(long int);
    break;
  case OPCODE_REDUCE_SUM_DOUBLE:
    type_size = sizeof(double);
    break;
  }
  code_put_pointer(ip, header_gpu_byte_code + *gpu_byte_code_counter, isdryrun);
  gpu_byte_code_flush1(*streams, gpu_byte_code, *gpu_byte_code_counter,
                       type_size);
  if (!isdryrun) {
    code_put_int(ip,
                 ((long int *)(gpu_byte_code + *gpu_byte_code_counter))[1] *
                     ((int *)(gpu_byte_code + *gpu_byte_code_counter))[1],
                 isdryrun);
  } else {
    code_put_int(ip, 0, isdryrun);
  }
  gpu_byte_code_flush2(streams, gpu_byte_code_counter);
}
#endif

int ext_mpi_generate_byte_code(char **shmem,
                               int shmem_size, int *shmemid,
                               char *buffer_in, char *sendbuf, char *recvbuf,
                               int my_size_shared_buf, int barriers_size, char *locmem,
                               int reduction_op, int *global_ranks,
                               char *code_out, int size_comm, int size_request, void *comm_row,
                               int node_num_cores_row, void *comm_column,
                               int node_num_cores_column,
                               int *shmemid_gpu, char **shmem_gpu, int *gpu_byte_code_counter, int tag) {
  struct line_memcpy_reduce data_memcpy_reduce;
  struct line_irecv_isend data_irecv_isend;
  char line[1000], *ip = code_out;
  enum eassembler_type estring1a, estring1, estring2;
  int integer1, integer2, integer3, integer4, isdryrun = (code_out == NULL),
      ascii, num_cores, socket_rank, num_sockets_per_node, i;
  struct header_byte_code header_temp;
  struct header_byte_code *header;
#ifdef GPU_ENABLED
  char *gpu_byte_code = NULL;
  int on_gpu, reduce, isend = 1, added = 0, vector_length;
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
  socket_rank = parameters->socket_rank;
  num_sockets_per_node = parameters->num_sockets_per_node;
  ext_mpi_delete_parameters(parameters);
  memset(&header_temp, 0, sizeof(struct header_byte_code));
  if (isdryrun) {
    header = &header_temp;
    header->num_cores = num_cores;
    header->socket_rank = socket_rank;
    header->num_sockets_per_node = num_sockets_per_node;
  } else {
    header = (struct header_byte_code *)ip;
    header->barrier_counter_socket = 1;
    header->barrier_counter_node = 1;
    if (shmem && shmem[0]) {
      header->barrier_shmem_node = (char **)malloc(num_sockets_per_node*sizeof(char *));
      for (i=0; i<num_sockets_per_node; i++) {
        header->barrier_shmem_node[i] = shmem[i] + my_size_shared_buf + 2 * barriers_size;
      }
      header->barrier_shmem_socket = shmem[0] + my_size_shared_buf;
    } else {
      header->barrier_shmem_node = NULL;
      header->barrier_shmem_socket = NULL;
    }
    header->barrier_shmem_size = barriers_size;
    header->shmemid = shmemid;
    header->locmem = locmem;
    header->shmem = shmem;
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
  }
#ifdef GPU_ENABLED
  if (on_gpu) {
    if (!isdryrun) {
      gpu_byte_code = (char *)malloc(*gpu_byte_code_counter);
      if (!gpu_byte_code)
        goto error;
      memset(gpu_byte_code, 0, *gpu_byte_code_counter);
      if (header->barrier_shmem_node) {
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
          flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code,
                         gpu_byte_code_counter, reduction_op, isdryrun);
          added = 0;
        }
      }
#endif
      code_put_char(&ip, OPCODE_RETURN, isdryrun);
    }
    if (estring1 == eset_socket_barrier) {
      if (header->num_cores != 1) {
#ifdef GPU_ENABLED
        if (on_gpu) {
          if (added) {
            flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code,
                           gpu_byte_code_counter, reduction_op, isdryrun);
            added = 0;
          }
          code_put_char(&ip, OPCODE_GPUSYNCHRONIZE, isdryrun);
        }
#endif
        code_put_char(&ip, OPCODE_SOCKETBARRIER_ATOMIC_SET, isdryrun);
        code_put_int(&ip, integer1, isdryrun);
      }
    }
    if (estring1 == ewait_socket_barrier) {
      if (header->num_cores != 1) {
        code_put_char(&ip, OPCODE_SOCKETBARRIER_ATOMIC_WAIT, isdryrun);
        code_put_int(&ip, integer1, isdryrun);
      }
    }
    if (estring1 == ewaitall) {
#ifdef GPU_ENABLED
      if (on_gpu) {
        if (added) {
          flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code,
                         gpu_byte_code_counter, reduction_op, isdryrun);
          added = 0;
        }
        isend = 1;
      }
#endif
      if (integer1) {
        code_put_char(&ip, OPCODE_MPIWAITALL, isdryrun);
        code_put_int(&ip, integer1, isdryrun);
        code_put_pointer(&ip, header->locmem, isdryrun);
      }
#ifdef GPU_ENABLED
      if (on_gpu && (header->num_cores == 1)) {
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
          flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code,
                         gpu_byte_code_counter, reduction_op, isdryrun);
          added = 0;
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
        if (on_gpu && (header->num_cores == 1) && isend) {
          code_put_char(&ip, OPCODE_GPUSYNCHRONIZE, isdryrun);
          isend = 0;
        }
#endif
        code_put_char(&ip, OPCODE_MPIISEND, isdryrun);
      } else {
        code_put_char(&ip, OPCODE_MPIIRECV, isdryrun);
      }
      if (estring2 == esendbufp) {
        code_put_pointer(&ip, sendbuf + integer1, isdryrun);
      } else {
        if (estring2 == erecvbufp) {
          code_put_pointer(&ip, recvbuf + integer1, isdryrun);
        } else {
	  if (shmem) {
            code_put_pointer(&ip, (void *)(shmem[data_irecv_isend.buffer_number] + integer1), isdryrun);
	  } else {
            code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	  }
        }
      }
      code_put_int(&ip, integer2, isdryrun);
      code_put_int(&ip, global_ranks[integer3], isdryrun);
      code_put_pointer(&ip, header->locmem + size_request * integer4, isdryrun);
    }
    if (estring1 == enode_barrier) {
      code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);
    }
    if (estring1 == esocket_barrier) {
      if (header->num_cores != 1) {
#ifdef GPU_ENABLED
        if (on_gpu) {
          if (added) {
            flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code,
                           gpu_byte_code_counter, reduction_op, isdryrun);
            added = 0;
          }
          code_put_char(&ip, OPCODE_GPUSYNCHRONIZE, isdryrun);
        }
#endif
        code_put_char(&ip, OPCODE_SOCKETBARRIER, isdryrun);
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
    if ((estring1 == ememcpy) || (estring1 == ereduce) ||
        (estring1 == esreduce) || (estring1 == esmemcpy)) {
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
          code_put_char(&ip, OPCODE_REDUCE, isdryrun);
          code_put_char(&ip, reduction_op, isdryrun);
          switch (reduction_op) {
          case OPCODE_REDUCE_SUM_DOUBLE:
            integer3 /= sizeof(double);
            break;
          case OPCODE_REDUCE_SUM_LONG_INT:
            integer3 /= sizeof(long int);
            break;
          case OPCODE_REDUCE_SUM_FLOAT:
            integer3 /= sizeof(float);
            break;
          case OPCODE_REDUCE_SUM_INT:
            integer3 /= sizeof(int);
            break;
          }
        }
        if (estring1a == esendbufp) {
          code_put_pointer(&ip, sendbuf + integer1, isdryrun);
        } else {
          if (estring1a == erecvbufp) {
            code_put_pointer(&ip, recvbuf + integer1, isdryrun);
          } else {
            if (shmem) {
              code_put_pointer(&ip, (void *)(shmem[data_memcpy_reduce.buffer_number1] + integer1), isdryrun);
	    } else {
              code_put_pointer(&ip, (void *)(NULL + integer1), isdryrun);
	    }
          }
        }
        if (estring2 == esendbufp) {
          code_put_pointer(&ip, sendbuf + integer2, isdryrun);
        } else {
          if (estring2 == erecvbufp) {
            code_put_pointer(&ip, recvbuf + integer2, isdryrun);
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
          p1 = sendbuf + integer1;
        } else {
          if (estring1a == erecvbufp) {
            p1 = recvbuf + integer1;
          } else {
            if (shmem) {
              p1 = (void *)(shmem[data_memcpy_reduce.buffer_number1] + integer1);
	    } else {
              p1 = (void *)(NULL + integer1);
	    }
          }
        }
        if (estring2 == esendbufp) {
          p2 = sendbuf + integer2;
        } else {
          if (estring2 == erecvbufp) {
            p2 = recvbuf + integer2;
          } else {
            if (shmem) {
              p2 = (void *)(shmem[data_memcpy_reduce.buffer_number2] + integer2);
	    } else {
              p2 = (void *)(NULL + integer2);
	    }
          }
        }
        if (gpu_byte_code_add(&streams, p1, p2, integer3, reduce) < 0) {
          flush_complete(&ip, &streams, header->gpu_byte_code, gpu_byte_code,
                         gpu_byte_code_counter, reduction_op, isdryrun);
          gpu_byte_code_add(&streams, p1, p2, integer3, reduce);
        }
        added = 1;
      }
#endif
    }
  }
#ifdef GPU_ENABLED
  if (!isdryrun && on_gpu) {
    if (header->barrier_shmem_node) {
      ext_mpi_gpu_memcpy_hd(header->gpu_byte_code, gpu_byte_code, *gpu_byte_code_counter);
    } else {
      memcpy(header->gpu_byte_code, gpu_byte_code, *gpu_byte_code_counter);
    }
    ext_mpi_gemv_init(reduction_op, vector_length, num_cores, &header->gpu_gemv_var);
  } else {
    header->gpu_gemv_var.handle = 0;
  }
  free(gpu_byte_code);
#endif
  header->size_to_return = ip - code_out;
  if (code_out) {
    if (comm_row) {
      memcpy(ip, comm_row, size_comm);
    } else {
      memset(ip, 0, size_comm);
    }
  }
  ip += size_comm;
  if (code_out) {
    if (comm_column) {
      memcpy(ip, comm_column, size_comm);
    } else {
      memset(ip, 0, size_comm);
    }
  }
  ip += size_comm;
  return (ip - code_out);
#ifdef GPU_ENABLED
error:
  return ERROR_MALLOC;
#endif
}
