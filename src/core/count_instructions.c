#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "allreduce.h"
#include "alltoall.h"
#include "backward_interpreter.h"
#include "buffer_offset.h"
#include "clean_barriers.h"
#include "dummy.h"
#include "ext_mpi_native.h"
#include "forward_interpreter.h"
#include "no_first_barrier.h"
#include "no_offset.h"
#include "optimise_buffers.h"
#include "optimise_buffers2.h"
#include "parallel_memcpy.h"
#include "rank_permutation.h"
#include "raw_code.h"
#include "raw_code_merge.h"
#include "raw_code_tasks_node.h"
#include "raw_code_tasks_node_master.h"
#include "read_write.h"
#include "reduce_copyin.h"
#include "reduce_copyout.h"
#include "byte_code.h"
#include "ports_groups.h"
#include "count_instructions.h"

int ext_mpi_allreduce_init_draft(void *sendbuf, void *recvbuf, int count,
                                 int type_size,
                                 int mpi_size_row, int my_cores_per_node_row,
                                 int mpi_size_column,
                                 int my_cores_per_node_column, int *num_ports,
                                 int *groups, int num_active_ports,
                                 int bit, char **code_address) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node,
      my_mpi_rank_column, my_lrank_column, my_lrank_node;
  int coarse_count, *counts = NULL;
  char *buffer1 = NULL, *buffer2 = NULL;
  int nbuffer1 = 0, msize, *msizes = NULL, *msizes2 = NULL, i,
      allreduce_short = (num_ports[0] > 0);
  int reduction_op=-1;
  char *ip, *str;
  int *global_ranks = NULL, code_size;
  int gpu_byte_code_counter = 0;
  if (allreduce_short) {
    for (i = 0; groups[i]; i++) {
      if ((groups[i] < 0) && groups[i + 1]) {
        allreduce_short = 0;
      }
    }
  }
  if (bit) {
    allreduce_short = 0;
  }
  buffer1 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer1)
    goto error;
  buffer2 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer2)
    goto error;
  if (type_size == sizeof(long int)) {
    reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
  }
  if (type_size == sizeof(int)) {
    reduction_op = OPCODE_REDUCE_SUM_INT;
  }
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
  }
  count *= type_size;
  my_mpi_size_row = mpi_size_row;
  my_mpi_rank_row = 0;
  my_mpi_rank_column = 0;
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  my_lrank_node = 0;
  my_cores_per_node_row = 1;
  my_cores_per_node_column = 1;
  counts = (int *)malloc(my_cores_per_node_column * sizeof(int));
  if (!counts)
    goto error;
  if (mpi_size_column > 1) {
    coarse_count = count * mpi_size_column;
    for (i = 0; i < my_cores_per_node_column; i++) {
      counts[i] = coarse_count;
    }
  } else {
    coarse_count = count;
    counts[0] = count;
  }
  msize = 0;
  for (i = 0; i < my_cores_per_node_column; i++) {
    msize += counts[i];
  }
  if (!allreduce_short) {
    msizes2 =
        (int *)malloc(sizeof(int) * my_mpi_size_row / my_cores_per_node_row);
    if (!msizes2)
      goto error;
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      msizes2[i] =
          (msize / type_size) / (my_mpi_size_row / my_cores_per_node_row);
    }
    for (i = 0;
         i < (msize / type_size) % (my_mpi_size_row / my_cores_per_node_row);
         i++) {
      msizes2[i]++;
    }
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      msizes2[i] *= type_size;
    }
    msizes =
        (int *)malloc(sizeof(int) * my_mpi_size_row / my_cores_per_node_row);
    if (!msizes)
      goto error;
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      msizes[i] = msizes2[i];
      msizes[i] = msizes2[0];
    }
    free(msizes2);
  } else {
    msizes = (int *)malloc(sizeof(int) * 1);
    if (!msizes)
      goto error;
    msizes[0] = msize;
  }
  if (allreduce_short) {
    nbuffer1 += sprintf(buffer1 + nbuffer1,
                        " PARAMETER COLLECTIVE_TYPE ALLREDUCE_SHORT\n");
  } else {
    nbuffer1 += sprintf(buffer1 + nbuffer1,
                        " PARAMETER COLLECTIVE_TYPE ALLREDUCE_GROUP\n");
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_SOCKETS %d\n",
                      my_mpi_size_row / my_cores_per_node_row);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER COPYIN_METHOD %d\n", 0);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER COPYIN_FACTORS");
  for (i = 1; i < my_cores_per_node_row * 2; i *= 2) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", 2);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER COUNTS");
  for (i = 0; i < my_cores_per_node_column; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", counts[i]);
  }
  free(counts);
  counts = NULL;
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  str = ext_mpi_print_ports_groups(num_ports, groups);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_PORTS %s\n", str);
  free(str);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER MESSAGE_SIZE");
  if (!allreduce_short) {
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", msizes[i]);
    }
  } else {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", msizes[0]);
    for (i = 1; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", 0);
    }
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  if (type_size == sizeof(long int)) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE LONG_INT\n");
  }
  if (type_size == sizeof(int)) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE INT\n");
  }
  if (bit) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER BIT_IDENTICAL\n");
  }
  //nbuffer1+=sprintf(buffer1+nbuffer1, " PARAMETER ASCII\n");
  free(msizes);
  msizes = NULL;
  if (!allreduce_short) {
    if (ext_mpi_generate_allreduce(buffer1, buffer2) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_allreduce(buffer1, buffer2) < 0)
      goto error;
  }
  if (ext_mpi_generate_raw_code_tasks_node(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_reduce_copyin(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_raw_code(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_reduce_copyout(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_buffer_offset(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_no_offset(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_optimise_buffers(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_optimise_buffers2(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_parallel_memcpy(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_raw_code_merge(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_no_first_barrier(buffer2, buffer1) < 0)
    goto error;
  global_ranks =
      (int *)malloc(sizeof(int) * my_mpi_size_row * my_cores_per_node_column);
  code_size = ext_mpi_generate_byte_code(NULL, 0, 0, buffer1, (char *)sendbuf,
                                         (char *)recvbuf, 0, 0, NULL, reduction_op,
                                         global_ranks, NULL, sizeof(MPI_Comm), sizeof(MPI_Request), NULL, 1,
                                         NULL, 1, NULL, NULL, &gpu_byte_code_counter, 0);
  if (code_size < 0)
    goto error;
  ip = *code_address = (char *)malloc(code_size);
  if (!ip)
    goto error;
  if (ext_mpi_generate_byte_code(NULL, 0, 0, buffer1, (char *)sendbuf, (char *)recvbuf,
                                 0, 0, NULL, reduction_op, global_ranks, ip,
                                 sizeof(MPI_Comm), sizeof(MPI_Request), NULL, 1, NULL, 1,
                                 NULL, NULL, &gpu_byte_code_counter, 0) < 0)
    goto error;
  free(global_ranks);
  free(buffer2);
  free(buffer1);
  return 0;
error:
  free(counts);
  free(msizes);
  free(msizes2);
  free(global_ranks);
  free(buffer2);
  free(buffer1);
  return ERROR_MALLOC;
}

static int counters_num_memcpy = 0;
static int counters_size_memcpy = 0;
static int counters_num_reduce = 0;
static int counters_size_reduce = 0;
static int counters_num_MPI_Irecv = 0;
static int counters_size_MPI_Irecv = 0;
static int counters_num_MPI_Isend = 0;
static int counters_size_MPI_Isend = 0;
static int counters_num_MPI_Recv = 0;
static int counters_size_MPI_Recv = 0;
static int counters_num_MPI_Send = 0;
static int counters_size_MPI_Send = 0;
static int counters_num_MPI_Sendrecv = 0;
static int counters_size_MPI_Sendrecv = 0;
static int counters_size_MPI_Sendrecvb = 0;

void ext_mpi_get_counters_native(struct ext_mpi_counters_native *var) {
  var->counters_num_memcpy = counters_num_memcpy;
  var->counters_size_memcpy = counters_size_memcpy;
  var->counters_num_reduce = counters_num_reduce;
  var->counters_size_reduce = counters_size_reduce;
  var->counters_num_MPI_Irecv = counters_num_MPI_Irecv;
  var->counters_size_MPI_Irecv = counters_size_MPI_Irecv;
  var->counters_num_MPI_Isend = counters_num_MPI_Isend;
  var->counters_size_MPI_Isend = counters_size_MPI_Isend;
  var->counters_num_MPI_Recv = counters_num_MPI_Recv;
  var->counters_size_MPI_Recv = counters_size_MPI_Recv;
  var->counters_num_MPI_Send = counters_num_MPI_Send;
  var->counters_size_MPI_Send = counters_size_MPI_Send;
  var->counters_num_MPI_Sendrecv = counters_num_MPI_Sendrecv;
  var->counters_size_MPI_Sendrecv = counters_size_MPI_Sendrecv;
  var->counters_size_MPI_Sendrecvb = counters_size_MPI_Sendrecvb;
}

void ext_mpi_set_counters_zero_native() {
  counters_num_memcpy = 0;
  counters_size_memcpy = 0;
  counters_num_reduce = 0;
  counters_size_reduce = 0;
  counters_num_MPI_Irecv = 0;
  counters_size_MPI_Irecv = 0;
  counters_num_MPI_Isend = 0;
  counters_size_MPI_Isend = 0;
  counters_num_MPI_Recv = 0;
  counters_size_MPI_Recv = 0;
  counters_num_MPI_Send = 0;
  counters_size_MPI_Send = 0;
  counters_num_MPI_Sendrecv = 0;
  counters_size_MPI_Sendrecv = 0;
  counters_size_MPI_Sendrecvb = 0;
}

int ext_mpi_simulate_native(char *ip) {
  char instruction, instruction2;
  int i1, i3, n_r, i;
  ip += sizeof(struct header_byte_code);
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      break;
    case OPCODE_MEMCPY:
      code_get_pointer(&ip);
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      counters_num_memcpy++;
      counters_size_memcpy += i1;
#ifdef VERBOSE_PRINT
      printf("memcpy %p %p %d\n", (void *)p1, (void *)p2, i1);
#endif
      break;
    case OPCODE_MPIIRECV:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      code_get_pointer(&ip);
      counters_num_MPI_Irecv++;
      counters_size_MPI_Irecv += i1;
#ifdef VERBOSE_PRINT
      printf("MPI_Irecv %p %d %d %p\n", (void *)p1, i1, i2, (void *)p2);
#endif
      break;
    case OPCODE_MPIISEND:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      code_get_pointer(&ip);
      counters_num_MPI_Isend++;
      counters_size_MPI_Isend += i1;
#ifdef VERBOSE_PRINT
      printf("MPI_Isend %p %d %d %p\n", (void *)p1, i1, i2, (void *)p2);
#endif
      break;
    case OPCODE_MPIWAITALL:
      i1 = code_get_int(&ip);
      code_get_pointer(&ip);
#ifdef VERBOSE_PRINT
      printf("MPI_Waitall %d %p\n", i1, p1);
#endif
      break;
    case OPCODE_MPIWAITANY:
      i1 = code_get_int(&ip);
      i1 = code_get_int(&ip);
      code_get_pointer(&ip);
#ifdef VERBOSE_PRINT
      printf("MPI_Waitany %d %p\n", i1, p1);
#endif
      break;
    case OPCODE_SOCKETBARRIER:
#ifdef VERBOSE_PRINT
      printf("socket_barrier\n");
#endif
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_SET:
      i1 = code_get_int(&ip);
#ifdef VERBOSE_PRINT
      printf("socket_barrier_atomic_set %d\n", i1);
#endif
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_WAIT:
      i1 = code_get_int(&ip);
#ifdef VERBOSE_PRINT
      printf("socket_barrier_atomic_wait %d\n", i1);
#endif
      break;
    case OPCODE_REDUCE:
      instruction2 = code_get_char(&ip);
      switch (instruction2) {
      case OPCODE_REDUCE_SUM_DOUBLE:
        code_get_pointer(&ip);
        code_get_pointer(&ip);
        i1 = code_get_int(&ip);
        counters_num_reduce++;
        counters_size_reduce += i1 * sizeof(double);
#ifdef VERBOSE_PRINT
        printf("REDUCE_SUM_DOUBLE %p %p %d\n", p1, p2, i1);
#endif
        break;
      case OPCODE_REDUCE_SUM_LONG_INT:
        code_get_pointer(&ip);
        code_get_pointer(&ip);
        i1 = code_get_int(&ip);
        counters_num_reduce++;
        counters_size_reduce += i1 * sizeof(long int);
#ifdef VERBOSE_PRINT
        printf("REDUCE_SUM_LONG_INT %p %p %d\n", p1, p2, i1);
#endif
        break;
      }
      break;
    case OPCODE_MPISENDRECV:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      code_get_pointer(&ip);
      i3 = code_get_int(&ip);
      code_get_int(&ip);
      counters_num_MPI_Sendrecv++;
      counters_size_MPI_Sendrecv += i1;
      counters_size_MPI_Sendrecvb += i3;
#ifdef VERBOSE_PRINT
      printf("MPI_Sendrecv %p %d %d %p %d %d\n", (void *)p1, i1, i2, (void *)p2,
             i3, i4);
#endif
      break;
    case OPCODE_MPISEND:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      counters_num_MPI_Send++;
      counters_size_MPI_Send += i1;
#ifdef VERBOSE_PRINT
      printf("MPI_Send %p %d %d\n", (void *)p1, i1, i2);
#endif
      break;
    case OPCODE_MPIRECV:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      counters_num_MPI_Recv++;
      counters_size_MPI_Recv += i1;
#ifdef VERBOSE_PRINT
      printf("MPI_Recv %p %d %d\n", (void *)p1, i1, i2);
#endif
      break;
    case OPCODE_ATTACHED:
#ifdef VERBOSE_PRINT
      printf("attached\n");
#endif
      break;
    case OPCODE_LOCALMEM:
      i1 = code_get_int(&ip);
      n_r = code_get_int(&ip);
#ifdef VERBOSE_PRINT
      printf("relocation %d %d ", i1, n_r);
#endif
      for (i = 0; i < n_r; i++) {
        code_get_int(&ip);
#ifdef VERBOSE_PRINT
        printf("%d ", i2);
#endif
      }
#ifdef VERBOSE_PRINT
      printf("\n");
#endif
      break;
#ifdef NCCL_ENABLED
    case OPCODE_START:
      break;
#endif
    default:
      printf("illegal MPI_OPCODE simulate_native\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  return (0);
}

int ext_mpi_count_native(char *ip, double *counts, int *num_steps) {
  char instruction, instruction2;
  int i1, i3, n_r, i, step = 0, substep = 0;
  double count = 0e0;
  ip += sizeof(struct header_byte_code);
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      break;
    case OPCODE_MEMCPY:
      code_get_pointer(&ip);
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      counters_num_memcpy++;
      counters_size_memcpy += i1;
      break;
    case OPCODE_MPIIRECV:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      code_get_pointer(&ip);
      counters_num_MPI_Irecv++;
      counters_size_MPI_Irecv += i1;
      break;
    case OPCODE_MPIISEND:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      code_get_pointer(&ip);
      counters_num_MPI_Isend++;
      counters_size_MPI_Isend += i1;
      substep++;
      if (i1 * 1e0 > count) {
        count = i1 * 1e0;
      }
      break;
    case OPCODE_MPIWAITALL:
      i1 = code_get_int(&ip);
      code_get_pointer(&ip);
      if (i1 > 0) {
        counts[step] = count;
        num_steps[step] = substep;
        step++;
        substep = 0;
        count = 0e0;
      }
      break;
    case OPCODE_MPIWAITANY:
      i1 = code_get_int(&ip);
      i1 = code_get_int(&ip);
      code_get_pointer(&ip);
      if (i1 > 0) {
        counts[step] = count;
        num_steps[step] = substep;
        step++;
        substep = 0;
        count = 0e0;
      }
      break;
    case OPCODE_SOCKETBARRIER:
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_SET:
      i1 = code_get_int(&ip);
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_WAIT:
      i1 = code_get_int(&ip);
      break;
    case OPCODE_REDUCE:
      instruction2 = code_get_char(&ip);
      code_get_pointer(&ip);
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      switch (instruction2) {
      case OPCODE_REDUCE_SUM_DOUBLE:
        counters_num_reduce++;
        counters_size_reduce += i1 * sizeof(double);
        break;
      case OPCODE_REDUCE_SUM_LONG_INT:
        counters_num_reduce++;
        counters_size_reduce += i1 * sizeof(long int);
        break;
      case OPCODE_REDUCE_SUM_FLOAT:
        counters_num_reduce++;
        counters_size_reduce += i1 * sizeof(float);
        break;
      case OPCODE_REDUCE_SUM_INT:
        counters_num_reduce++;
        counters_size_reduce += i1 * sizeof(int);
        break;
      }
      break;
    case OPCODE_MPISENDRECV:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      code_get_pointer(&ip);
      i3 = code_get_int(&ip);
      code_get_int(&ip);
      counters_num_MPI_Sendrecv++;
      counters_size_MPI_Sendrecv += i1;
      counters_size_MPI_Sendrecvb += i3;
      printf("not implemented\n");
      exit(2);
      break;
    case OPCODE_MPISEND:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      counters_num_MPI_Send++;
      counters_size_MPI_Send += i1;
      printf("not implemented\n");
      exit(2);
      break;
    case OPCODE_MPIRECV:
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      counters_num_MPI_Recv++;
      counters_size_MPI_Recv += i1;
      break;
    case OPCODE_LOCALMEM:
      i1 = code_get_int(&ip);
      n_r = code_get_int(&ip);
      for (i = 0; i < n_r; i++) {
        code_get_int(&ip);
      }
      break;
    case OPCODE_ATTACHED:
      break;
#ifdef GPU_ENABLED
    case OPCODE_GPUSYNCHRONIZE:
      break;
    case OPCODE_GPUKERNEL:
      instruction2 = code_get_char(&ip);
      code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      switch (instruction2) {
      case OPCODE_REDUCE_SUM_DOUBLE:
        break;
      case OPCODE_REDUCE_SUM_LONG_INT:
        break;
      }
      break;
#endif
#ifdef NCCL_ENABLED
    case OPCODE_START:
      break;
#endif
    default:
      printf("illegal MPI_OPCODE count_native %d\n", instruction);
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  counts[step] = 0e0;
  num_steps[step] = 0;
  return (0);
}
