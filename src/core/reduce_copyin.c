#include "reduce_copyin.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct memory_layout {
  int size;
  int offset;
  int rank;
};

static int write_memcpy_reduce(enum eassembler_type type, enum eassembler_type buffer_type1, int buffer_number1, int is_fast1, int offset1, enum eassembler_type buffer_type2, int buffer_number2, int is_fast2, int offset2, int size, int aoffset, char *buffer_out, int ascii) {
  struct line_memcpy_reduce data_memcpy_reduce;
  data_memcpy_reduce.type = type;
  data_memcpy_reduce.buffer_type1 = buffer_type1;
  data_memcpy_reduce.buffer_number1 = buffer_number1;
  data_memcpy_reduce.is_offset1 = is_fast1;
  data_memcpy_reduce.offset_number1 = -1;
  data_memcpy_reduce.offset1 = offset1 + aoffset;
  data_memcpy_reduce.buffer_type2 = buffer_type2;
  data_memcpy_reduce.buffer_number2 = buffer_number2;
  data_memcpy_reduce.is_offset2 = is_fast2;
  data_memcpy_reduce.offset_number2 = -1;
  data_memcpy_reduce.offset2 = offset2;
  data_memcpy_reduce.size = size;
  return ext_mpi_write_memcpy_reduce(buffer_out, &data_memcpy_reduce, ascii);
}

static int local_barrier(int num_all_ranks, int num_busy_ranks, int *ranks, char *buffer_out, int ascii) {
  int nbuffer_out = 0, nsteps, step, fac, final;
  final = (num_all_ranks < 0);
  num_all_ranks = abs(num_all_ranks);
  if (!final) {
    for (step = 0, fac = 1; fac < num_busy_ranks; step++, fac *= 2) {
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", eset_socket_barrier, ranks[0]);
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, ranks[fac % num_busy_ranks]);
    }
    for (nsteps = 0, fac = 1; fac < num_all_ranks; nsteps++, fac *= 2)
      ;
    for (; step < nsteps; step++) {
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", eset_socket_barrier, ranks[0]);
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, ranks[0]);
    }
  } else {
    if (num_busy_ranks > 0) {
      for (step = 0, fac = 1; fac < num_busy_ranks; step++, fac *= 2) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", eset_socket_barrier, ranks[0]);
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, ranks[fac % num_busy_ranks]);
      }
    } else {
      for (step = 0, fac = 1; fac < -num_busy_ranks; step++, fac *= 2) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", eset_socket_barrier, ranks[0]);
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, ranks[0]);
      }
    }
  }
  return nbuffer_out;
}

static int reduce_copyin(struct data_algorithm *data, int num_nodes, int counts_max, int *mcounts, int *moffsets, int *ldispls, int lrank_row, int lrank_column, int type_size, int instance, int instance_max, int gbstep, int num_ranks, int *ranks, int fast, int aoffset, char *buffer_out, int ascii) {
  int nbuffer_out = 0, i, j, n, add, add2, size, add_local, size_local, add_appl, add2_appl, size_appl, add_offset, lrank_row_;
  for (n = 0; n < 2; n++) {
    for (i = 0; i < num_ranks / counts_max; i++) {
      if (!fast) {
        if (n == 0) {
          add_offset = ldispls[lrank_column] + instance * moffsets[num_nodes];
        } else {
          add_offset = ldispls[lrank_column] + instance_max * moffsets[num_nodes];
        }
      } else {
        if (n == 0) {
          add_offset = ldispls[lrank_column] + instance * CACHE_LINE_SIZE + OFFSET_FAST;
        } else {
          add_offset = ldispls[lrank_column] + instance_max * CACHE_LINE_SIZE + OFFSET_FAST;
        }
      }
      add = 0;
      lrank_row_ = (2 * num_ranks / counts_max - 1 + lrank_row - i) % (num_ranks / counts_max);
      size_local = (moffsets[num_nodes] / type_size) / (num_ranks / counts_max);
      if (lrank_row_ >= (moffsets[num_nodes] / type_size) % (num_ranks / counts_max)) {
        add_local = size_local * lrank_row_ + (moffsets[num_nodes] / type_size) % (num_ranks / counts_max);
      } else {
        add_local = size_local * lrank_row_ + lrank_row_;
        size_local++;
      }
      size_local *= type_size;
      add_local *= type_size;
      for (j = 0; j < data->blocks[0].num_lines; j++) {
        size = mcounts[data->blocks[0].lines[j].frac];
        add2 = moffsets[data->blocks[0].lines[j].frac];
        add_appl = -1;
        if (!(add_local + size_local <= add || add + size <= add_local)) {
          add_appl = add_local;
          size_appl = size_local;
          if (add > add_appl) {
            size_appl -= add - add_appl;
            add_appl = add;
          }
          if (add + size < add_appl + size_appl) {
            size_appl = add + size - add_appl;
          }
          add2_appl = (moffsets[num_nodes] + add_appl - add + add2) % moffsets[num_nodes];
          add_appl = (moffsets[num_nodes] + add_appl) % moffsets[num_nodes];
	  add_appl += add_offset;
        } else {
          size_appl = 0;
        }
        if (size_appl) {
          if (i == 0) {
            if (n == 0) {
              nbuffer_out += write_memcpy_reduce(ememcpy, eshmemo, 0, fast, add_appl, esendbufp, 0, 0, add2_appl, size_appl, aoffset, buffer_out + nbuffer_out, ascii);
	    } else {
              nbuffer_out += write_memcpy_reduce(ememcp_, eshmemo, 0, 0, add_appl, esendbufp, 0, 0, add2_appl, size_appl, aoffset, buffer_out + nbuffer_out, ascii);
	    }
          } else {
	    if (n == 0) {
              nbuffer_out += write_memcpy_reduce(ereduce, eshmemo, 0, fast, add_appl, esendbufp, 0, 0, add2_appl, size_appl, aoffset, buffer_out + nbuffer_out, ascii);
            } else {
              nbuffer_out += write_memcpy_reduce(ereduc_, eshmemo, 0, 0, add_appl, esendbufp, 0, 0, add2_appl, size_appl, aoffset, buffer_out + nbuffer_out, ascii);
	    }
          }
        }
        add += size;
      }
      if (n == 0 && i < num_ranks / counts_max - 1) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", eset_socket_barrier, ranks[0]);
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, ranks[num_ranks - 1]);
      }
    }
  }
  nbuffer_out += local_barrier(-num_ranks, num_ranks, ranks, buffer_out + nbuffer_out, ascii);
  return nbuffer_out;
}

static int reduce_copies(int socket_size, int num_factors, int *factors, int size, int type_size, int rank, int fast, int subset, char *buffer_out, int ascii){
  struct memory_layout *memory_old, *memory_new;
  int nbuffer_out = 0, step, gbstep, size_local, add_local, i, j, k, ranks[socket_size], offset1, offset2, num_busy_ranks, vsocket_size;
  for (i = 0, vsocket_size = 1; i < num_factors; vsocket_size *= factors[i++])
    ;
  memory_old = (struct memory_layout*)malloc(vsocket_size * sizeof(struct memory_layout));
  memory_new = (struct memory_layout*)malloc(vsocket_size * sizeof(struct memory_layout));
  memory_old[0].size = size;
  memory_old[0].offset = 0;
  memory_old[0].rank = 0;
  for (i = 1; i < vsocket_size; i++) {
    memory_old[i].size = size;
    memory_old[i].offset = memory_old[i - 1].offset + memory_old[i - 1].size;
    if (i < socket_size) {
      memory_old[i].rank = i;
    } else {
      memory_old[i].rank = -1;
    }
  }
  for (step = 0, gbstep = 1; step < num_factors; gbstep *= factors[step++]) {
    for (i = 0; i < vsocket_size / gbstep / factors[step]; i++) {
      for (j = 0; j < gbstep; j++) {
        for (k = 0; k < factors[step]; k++) {
          memory_new[i * gbstep * factors[step] + j * factors[step] + k].rank = memory_old[i * gbstep * factors[step] + j + k * gbstep].rank;
	  if (memory_old[i * gbstep * factors[step] + j].size / factors[step] >= CACHE_LINE_SIZE) {
	    size_local = (memory_old[i * gbstep * factors[step] + j + k * gbstep].size / type_size / factors[step]) * type_size;
	    add_local = k * size_local;
	    if (k < (memory_old[i * gbstep * factors[step] + j + k * gbstep].size / type_size) % factors[step]) {
	      size_local += type_size;
              add_local += k * type_size;
            } else {
              add_local += ((memory_old[i * gbstep * factors[step] + j + k * gbstep].size / type_size) % factors[step]) * type_size;
            }
	  } else {
	    if (k == 0) {
	      size_local = memory_old[i * gbstep * factors[step] + j].size;
	    } else {
	      size_local = 0;
	    }
            add_local = 0;
	  }
          memory_new[i * gbstep * factors[step] + j * factors[step] + k].size = size_local;
          memory_new[i * gbstep * factors[step] + j * factors[step] + k].offset = memory_old[i * gbstep * factors[step] + j].offset + add_local;
	  if (memory_old[i * gbstep * factors[step] + j].offset >= size * socket_size) {
	    memory_new[i * gbstep * factors[step] + j * factors[step] + k].size = 0;
	    memory_new[i * gbstep * factors[step] + j * factors[step] + k].offset = -1;
	  }
	}
      }
    }
    for (i = 0; i < vsocket_size; i++) {
      memory_old[i] = memory_new[i];
    }
    j = -1;
    for (i = 0; i < vsocket_size; i++) {
      if (rank == memory_new[i].rank) {
	j = i;
      }
    }
    if (j < 0) j = 0;
    i = (j / factors[step]) * factors[step];
    for (k = 0; k < factors[step]; k++) {
      ranks[(factors[step] + k - (j - i)) % factors[step]] = memory_new[i + k].rank;
    }
    num_busy_ranks = 0;
    for (i = 0; i < factors[step]; i++) {
      for (k = 0; k < vsocket_size; k++) {
        if (ranks[i] == memory_new[k].rank && memory_new[k].size) {
	  num_busy_ranks = factors[step];
        }
      }
    }
    for (i = 0; i < num_busy_ranks; i++) {
      while (ranks[i] < 0 && i < num_busy_ranks) {
	for (k = i; k < num_busy_ranks - 1; k++) {
	  ranks[k] = ranks[k + 1];
	}
	num_busy_ranks--;
      }
    }
    if (rank >= socket_size) {
      num_busy_ranks = 0;
      ranks[0] = rank;
    }
    if (step >= 1) {
      nbuffer_out += local_barrier(factors[step], num_busy_ranks, ranks, buffer_out + nbuffer_out, ascii);
      if (memory_new[j].size) {
        for (i = 1; i < factors[step]; i++) {
	  offset1 = memory_new[j].offset;
	  offset2 = memory_new[j].offset + i * size * gbstep;
	  if (!fast) {
	    offset1 = offset1 % size + (offset1 / size) * size / factors[0];
	    offset2 = offset2 % size + (offset2 / size) * size / factors[0];
	  } else {
	    offset1 = offset1 % size + (offset1 / size) / factors[0] * CACHE_LINE_SIZE + OFFSET_FAST;
	    offset2 = offset2 % size + (offset2 / size) / factors[0] * CACHE_LINE_SIZE + OFFSET_FAST;
	  }
	  if (rank < vsocket_size) {
            nbuffer_out += write_memcpy_reduce(esreduce, eshmemo, 0, fast, offset1, eshmemo, 0, fast, offset2, memory_new[j].size, 0, buffer_out + nbuffer_out, ascii);
	  }
        }
      }
    }
  }
  num_busy_ranks = 0;
  for (i = 0; i < vsocket_size; i++) {
    if (memory_new[i].size) {
      ranks[num_busy_ranks] = memory_new[i].rank;
      num_busy_ranks++;
    }
  }
  for (i = 0; i < num_busy_ranks; i++) {
    if (ranks[i] == rank) {
      num_busy_ranks *= -1;
    }
  }
  num_busy_ranks *= -1;
  if (num_busy_ranks > 0) {
    while (ranks[0] != rank) {
      j = ranks[0];
      for (i = 0; i < num_busy_ranks - 1; i++) {
        ranks[i] = ranks[i + 1];
      }
      ranks[num_busy_ranks - 1] = j;
    }
  } else {
    ranks[0] = rank;
  }
  if (socket_size == num_busy_ranks && !subset) {
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", esocket_barrier);
  } else {
    nbuffer_out += local_barrier(-socket_size, num_busy_ranks, ranks, buffer_out + nbuffer_out, ascii);
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", eset_socket_barrier, rank);
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, 0);
  }
  free(memory_new);
  free(memory_old);
  return nbuffer_out;
}

static int reduce_copies_almost_half(int num_instances, int hsocket_size, int size, int type_size, int rank, int fast, char *buffer_out, int ascii) {
  int nbuffer_out = 0, offset1, offset2, size_local, add_local, nparallel, lrank, ranks[num_instances], i, j;
  nparallel = num_instances / (num_instances - hsocket_size);
nparallel = 2;
  if (rank < (num_instances - hsocket_size) * (nparallel - 1) || rank >= hsocket_size) {
    if (rank < (num_instances - hsocket_size) * (nparallel - 1)) {
      for (i = 0; i < nparallel - 1; i++) {
        ranks[i] = rank % (num_instances - hsocket_size) + (num_instances - hsocket_size) * i;
      }
      ranks[i] = rank % (num_instances - hsocket_size) + hsocket_size;
    } else {
      for (i = 0; i < nparallel - 1; i++) {
        ranks[i] = (rank - hsocket_size) % (num_instances - hsocket_size) + (num_instances - hsocket_size) * i;
      }
      ranks[i] = rank;
    }
    while (ranks[0] != rank) {
      j = ranks[0];
      for (i = 0; i < nparallel - 1; i++) {
        ranks[i] = ranks[i + 1];
      }
      ranks[nparallel - 1] = j;
    }
    nbuffer_out += local_barrier(-nparallel, nparallel, ranks, buffer_out + nbuffer_out, ascii);
    size_local = (size / type_size) / nparallel;
    if (rank < (num_instances - hsocket_size) * (nparallel - 1)) {
      lrank = rank / (num_instances - hsocket_size);
    } else {
      lrank = nparallel - 1;
    }
    if (lrank >= (size / type_size) % nparallel) {
      add_local = size_local * lrank + (size / type_size) % nparallel;
    } else {
      add_local = size_local * lrank + lrank;
      size_local++;
    }
    size_local *= type_size;
    add_local *= type_size;
    if (rank < (num_instances - hsocket_size) * (nparallel - 1)) {
      lrank = rank % (num_instances - hsocket_size);
    } else {
      lrank = rank - hsocket_size;
    }
    offset1 = lrank * size + add_local;
    offset2 = (lrank + hsocket_size) * size + add_local;
    nbuffer_out += write_memcpy_reduce(esreduce, eshmemo, 0, fast, offset1, eshmemo, 0, fast, offset2, size_local, 0, buffer_out + nbuffer_out, ascii);
    nbuffer_out += local_barrier(-nparallel, nparallel, ranks, buffer_out + nbuffer_out, ascii);
  } else {
    ranks[0] = rank;
    nbuffer_out += local_barrier(-nparallel, -nparallel, ranks, buffer_out + nbuffer_out, ascii);
    nbuffer_out += local_barrier(-nparallel, -nparallel, ranks, buffer_out + nbuffer_out, ascii);
  }
  return nbuffer_out;
}

/*static int reduce_copies_big(int num_instances, int socket_size, int num_busy_ranks, int size, int type_size, int rank, int fast, char *buffer_out, int ascii){
  int nbuffer_out = 0, add_local, size_local, i, ranks[socket_size], offset1, offset2;
  nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", esocket_barrier);
  if (rank < num_busy_ranks) {
    size_local = (size / type_size) / num_busy_ranks;
    if (rank >= (size / type_size) % num_busy_ranks) {
      add_local = size_local * rank + (size / type_size) % num_busy_ranks;
    } else {
      add_local = size_local * rank + rank;
      size_local++;
    }
    size_local *= type_size;
    add_local *= type_size;
    for (i = 0; i < num_instances - 1; i++) {
      if (!fast) {
        offset1 = add_local;
        offset2 = add_local + ((num_instances - 1 + i + rank/2 - 1) % (num_instances - 1) + 1) * size;
      } else {
        offset1 = add_local + OFFSET_FAST;
        offset2 = add_local + ((num_instances - 1 + i + rank/2 - 1) % (num_instances - 1) + 1) * CACHE_LINE_SIZE + OFFSET_FAST;
      }
      nbuffer_out += write_memcpy_reduce(esreduce, eshmemo, 0, fast, offset1, eshmemo, 0, fast, offset2, size_local, buffer_out + nbuffer_out, ascii);
    }
    for (i = 0; i < num_busy_ranks; i++) {
      ranks[i] = (i + rank) % num_busy_ranks;
    }
  } else {
    ranks[0] = rank;
  }
  if (socket_size == num_busy_ranks) {
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", esocket_barrier);
  } else {
    if (rank >= num_busy_ranks) {
      num_busy_ranks *= -1;
    }
    nbuffer_out += local_barrier(-socket_size, num_busy_ranks, ranks, buffer_out + nbuffer_out, ascii);
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", eset_socket_barrier, rank);
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, 0);
  }
  return nbuffer_out;
}*/

static void rank_order(int size, int num_factors, int *factors, int *ranks) {
  int ranks_new[size], ranks_temp[size], i, j;
  for (i = 0; i < size / abs(factors[0]); i++) {
    for (j = 0; j < abs(factors[0]); j++) {
      ranks_new[i + j * size / abs(factors[0])] = ranks[i * abs(factors[0]) + j];
    }
  }
  if (num_factors > 1) {
    for (j = 0; j < abs(factors[0]); j++) {
      for (i = 0; i < size / abs(factors[0]); i++) {
        ranks_temp[i] = ranks_new[i + j * size / abs(factors[0])];
      }
      rank_order(size / abs(factors[0]), num_factors - 1, factors + 1, ranks_temp);
      for (i = 0; i < size / abs(factors[0]); i++) {
        ranks[i + j * size / abs(factors[0])] = ranks_temp[i];
      }
    }
  }
}

static void sizes_displs(int socket_size, int size, int type_size, int size_l, int *sizes, int *displs) {
  int size_local, add_local, i;
  if (!size_l) {
    for (i = 0; i < socket_size; i++) {
      size_local = (size / type_size) / socket_size;
      if (i >= (size / type_size) % socket_size) {
        add_local = size_local * i + (size / type_size) % socket_size;
      } else {
        add_local = size_local * i + i;
        size_local++;
      }
      sizes[i] = size_local * type_size;
      displs[i] = add_local * type_size;
    }
  } else {
    for (i = 0; i < socket_size; i++) {
      if (i == 0) {
	displs[i] = 0;
      } else {
	displs[i] = displs[i - 1] + sizes[i - 1];
      }
      if (displs[i] + size_l * type_size <= size && i < socket_size - 1) {
        sizes[i] = size_l * type_size;
      } else {
	sizes[i] = size - displs[i];
      }
    }
  }
  displs[socket_size] = displs[socket_size - 1] + sizes[socket_size - 1];
}

static int reduce_copies_redundant(int socket_size, int num_factors, int *factors, int size, int type_size, int rank, int num_ranks, int *ranks, int offset_global, int expart, int first_call, char *buffer_out, int ascii){
  int nbuffer_out = 0, dist, depth, step, gbstep, substep, rstart = 0, i, j;
  for (depth = 0; factors[depth] < 0 && depth < num_factors; depth++);
  for (step = 0, gbstep = 1; step < num_factors && factors[step] < 0; gbstep *= abs(factors[step++]));
  dist = size;
  if (dist < CACHE_LINE_SIZE) dist = CACHE_LINE_SIZE;
  if (first_call) {
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", eset_socket_barrier);
    nbuffer_out += write_memcpy_reduce(esmemcpy, erecvbufp, 0, 0, offset_global, esendbufp, 0, 0, offset_global, size, 0, buffer_out + nbuffer_out, ascii);
    for (j = 0; j < socket_size; j++) {
      i = (j + rank) % socket_size;
      if (i != rank) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[i] - ranks[rank]) % num_ranks);
        nbuffer_out += write_memcpy_reduce(esreduce, erecvbufp, 0, 0, offset_global, esendbufp, (num_ranks + ranks[i] - ranks[rank]) % num_ranks, 0, offset_global, size, 0, buffer_out + nbuffer_out, ascii);
      }
    }
//    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", esocket_barrier);
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", eset_socket_barrier);
    for (j = 0; j < socket_size; j++) {
      i = (j + rank) % socket_size;
      if (i != rank) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[i] - ranks[rank]) % num_ranks);
      }
    }
  } else {
    nbuffer_out += write_memcpy_reduce(esmemcp_, eshmemo, 0, 0, offset_global + dist * depth, eshmemo, 0, 0, offset_global, size, 0, buffer_out + nbuffer_out, ascii);
    nbuffer_out += write_memcpy_reduce(esmemcpy, eshmemo, 0, 0, offset_global + dist * depth, erecvbufp, 0, 0, offset_global, size, 0, buffer_out + nbuffer_out, ascii);
    for (step = 0; step < num_factors && factors[step] < 0; step++) {
      gbstep /= abs(factors[step]);
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", eset_socket_barrier);
      nbuffer_out += write_memcpy_reduce(esmemcpy, eshmemo, 0, 0, offset_global + dist * (depth + 1 + step), eshmemo, 0, 0, offset_global + dist * (depth + step), size, 0, buffer_out + nbuffer_out, ascii);
      for (substep = 0; substep < abs(factors[step]); substep++) {
        if ((rank - rstart) / gbstep == substep) substep++;
        if (substep < abs(factors[step])) {
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, ranks[rstart + gbstep * substep + rank % gbstep]);
          nbuffer_out += write_memcpy_reduce(esreduce, eshmemo, 0, 0, offset_global + dist * (depth + 1 + step), eshmemo, (num_ranks + ranks[rstart + gbstep * substep + rank % gbstep] - ranks[rank]) % num_ranks, 0, offset_global + dist * ((depth + 1) + step), size, 0, buffer_out + nbuffer_out, ascii);
	}
      }
      rstart += ((rank - rstart) / gbstep) * gbstep;
    }
    nbuffer_out += write_memcpy_reduce(esmemcpy, erecvbufp, 0, 0, offset_global, eshmemo, 0, 0, offset_global + dist * 2 * depth, size, 0, buffer_out + nbuffer_out, ascii);
  }
  return nbuffer_out;
}

static int reduce_copies_cyclic(int socket_size, int num_factors, int *factors, int size, int type_size, int rank, int num_ranks, int *ranks, int size_l, int offset_global, int expart, int first_call, char *buffer_out, int ascii){
  int nbuffer_out = 0, size_local, offset, sizes[socket_size], displs[socket_size + 1], step, gbstep, substep, used[socket_size], first_used[socket_size], flag, i;
  for (i = 0; i < socket_size; i++) {
    used[i] = first_used[i] = 0;
  }
  sizes_displs(socket_size, size, type_size, size_l, sizes, displs);
  for (step = 0, gbstep = 1; step < num_factors && factors[step] < 0; gbstep *= abs(factors[step++]));
  for (step = 0; step < num_factors && factors[step] < 0; step++) {
    gbstep /= abs(factors[step]);
    if (expart == 0 || expart == 1) {
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", eset_socket_barrier);
    }
    for (substep = 1; substep < abs(factors[step]); substep++) {
      flag = 0;
      for (i = 0; i + gbstep * substep < socket_size && i < gbstep; i++) {
	size_local = sizes[(2 * socket_size + rank + i) % socket_size];
	if (size_local > 0) {
	  flag = 1;
	}
      }
      if (flag && (expart == 0 || expart == 1)) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[((socket_size - gbstep * substep) % socket_size + rank) % socket_size] - ranks[rank]) % num_ranks);
      }
      for (i = 0; i + gbstep * substep < socket_size && i < gbstep; i++) {
	offset = displs[(2 * socket_size + rank + i) % socket_size] + offset_global;
	size_local = sizes[(2 * socket_size + rank + i) % socket_size];
	if (first_call && !used[i + gbstep * substep]) {
	  if (substep == 1 && !first_used[i]) {
            if (size_local > 0 && (expart == 0 || expart == 1)) {
              nbuffer_out += write_memcpy_reduce(esmemcpy, erecvbufp, 0, 0, offset, esendbufp, 0, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
	    }
            first_used[i] = 1;
	  }
	  if (size_local > 0 && (expart == 0 || expart == 1)) {
            nbuffer_out += write_memcpy_reduce(esreduce, erecvbufp, 0, 0, offset, esendbufp, (num_ranks + ranks[((socket_size - gbstep * substep) % socket_size + rank) % socket_size] - ranks[rank]) % num_ranks, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
	  }
	} else {
          if (size_local > 0 && (expart == 0 || expart == 1)) {
            nbuffer_out += write_memcpy_reduce(esreduce, erecvbufp, 0, 0, offset, erecvbufp, (num_ranks + ranks[((socket_size - gbstep * substep) % socket_size + rank) % socket_size] - ranks[rank]) % num_ranks, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
	  }
	}
	used[i] = used[i + gbstep * substep] = 1;
      }
    }
  }
  if (expart == 0 || expart == 2) {
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", eset_socket_barrier);
  }
  if (step < num_factors) {
    for (gbstep = 1; step < num_factors; gbstep *= factors[step++]) {
      flag = 0;
      for (substep = 1; substep < factors[step]; substep++) {
        for (i = 0; i + gbstep * substep < socket_size && i < gbstep; i++) {
          size_local = sizes[(2 * socket_size + rank + i) % socket_size];
	  if (size_local > 0) {
	    flag = 1;
	  }
	}
      }
      if (gbstep > 1 || abs(factors[step - 1]) != factors[step]) {
	for (substep = 1; substep < abs(factors[step - 1]); substep++) {
          if (flag && (expart == 0 || expart == 2)) {
            nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[((socket_size + gbstep * substep / abs(factors[step - 1])) % socket_size + rank) % socket_size] - ranks[rank]) % num_ranks);
	  }
	}
      }
      for (substep = 1; substep < factors[step]; substep++) {
        for (i = 0; i + gbstep * substep < socket_size && i < gbstep; i++) {
          offset = displs[(2 * socket_size + rank + i) % socket_size] + offset_global;
          size_local = sizes[(2 * socket_size + rank + i) % socket_size];
          if (size_local > 0 && (expart == 0 || expart == 2)) {
            nbuffer_out += write_memcpy_reduce(esmemcpy, erecvbufp, (num_ranks + ranks[((socket_size - gbstep * substep) % socket_size + rank) % socket_size] - ranks[rank]) % num_ranks, 0, offset, erecvbufp, 0, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
	  }
        }
      }
      if (expart == 0 || expart == 2) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", eset_socket_barrier);
      }
    }
    for (substep = 1; substep < factors[step - 1]; substep++) {
      if (expart == 0 || expart == 2) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[((socket_size + gbstep * substep / factors[step - 1]) % socket_size + rank) % socket_size] - ranks[rank]) % num_ranks);
      }
    }
  } else {
    for (i = 1; i < socket_size; i++) {
      offset = displs[(socket_size + rank + i) % socket_size] + offset_global;
      size_local = sizes[(socket_size + rank + i) % socket_size];
      if (size_local > 0 && (expart == 0 || expart == 2)) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[(rank + i) % socket_size] - ranks[rank]) % num_ranks);
        nbuffer_out += write_memcpy_reduce(esmemcpy, erecvbufp, 0, 0, offset, erecvbufp, (num_ranks + ranks[(rank + i) % socket_size] - ranks[rank]) % num_ranks, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
      }
    }
    if (expart == 0 || expart == 2) {
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", esocket_barrier);
    }
  }
  return nbuffer_out;
}

static int reduce_copies_recursive(int socket_size, int num_factors, int *factors, int size, int type_size, int rank, int num_ranks, int *ranks, int size_l, int offset_global, int expart, int first_call, char *buffer_out, int ascii){
  int nbuffer_out = 0, size_local, offset, sizes[socket_size], displs[socket_size + 1], step, gbstep, substep, rstart = 0, rstart_next, flag_first = 1, flag, i, j;
  sizes_displs(socket_size, size, type_size, size_l, sizes, displs);
  for (step = 0, gbstep = 1; step < num_factors && factors[step] < 0; gbstep *= abs(factors[step++]));
  for (step = 0; step < num_factors && factors[step] < 0; step++) {
    gbstep /= abs(factors[step]);
    if (expart == 0 || expart == 1) {
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", eset_socket_barrier);
    }
    for (substep = 0; substep < abs(factors[step]); substep++) {
      if ((rank - rstart) / gbstep == substep) substep++;
      if (substep < abs(factors[step])) {
        offset = displs[(rank / gbstep) * gbstep] + offset_global;
	size_local = 0;
        for (i = 0; i < gbstep; i++) {
          size_local += sizes[(rank / gbstep) * gbstep + i];
        }
        if (size_local > 0 && (expart == 0 || expart == 1)) {
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[(rank + (socket_size - rank + rstart + gbstep * substep + rank % gbstep) % socket_size) % socket_size] - ranks[rank]) % num_ranks);
	  if (first_call && step == 0) {
	    if (flag_first) {
              nbuffer_out += write_memcpy_reduce(esmemcpy, erecvbufp, 0, 0, offset, esendbufp, 0, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
              flag_first = 0;
	    }
            nbuffer_out += write_memcpy_reduce(esreduce, erecvbufp, 0, 0, offset, esendbufp, (num_ranks + ranks[(rank + (socket_size - rank + rstart + gbstep * substep + rank % gbstep) % socket_size) % socket_size] - ranks[rank]) % num_ranks, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
	  } else {
            nbuffer_out += write_memcpy_reduce(esreduce, erecvbufp, 0, 0, offset, erecvbufp, (num_ranks + ranks[(rank + (socket_size - rank + rstart + gbstep * substep + rank % gbstep) % socket_size) % socket_size] - ranks[rank]) % num_ranks, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
	  }
	}
      }
    }
    rstart += ((rank - rstart) / gbstep) * gbstep;
  }
  if (expart == 0 || expart == 2) {
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", eset_socket_barrier);
  }
  if (step < num_factors) {
    for (gbstep = 1; step < num_factors; gbstep *= factors[step++]) {
      for (j = 1, i = num_factors - 1; factors[i] > 0; i--) {
	j *= factors[i];
      }
      rstart = 0;
      for (i = num_factors - 1; i > step; i--) {
	j /= factors[i];
        rstart += ((rank - rstart) / j) * j;
      }
      j /= factors[step];
      rstart_next = rstart + ((rank - rstart) / j) * j;
      flag = 0;
      for (substep = 0; substep < factors[step]; substep++) {
	if ((rank - rstart) / gbstep == substep) substep++;
	if (substep < factors[step]) {
          offset = displs[(rank / gbstep) * gbstep] + offset_global;
	  size_local = 0;
          for (i = 0; i < gbstep; i++) {
            size_local += sizes[(rank / gbstep) * gbstep + i];
          }
	  if (size_local > 0) {
	    flag = 1;
	  }
        }
      }
      if (gbstep > 1) {
	for (i = 0; i < abs(factors[step - 1]); i++) {
	  if (rank != rstart_next + gbstep * i / abs(factors[step - 1]) + rank % (gbstep / abs(factors[step - 1]))) {
	    if (flag && (expart == 0 || expart == 2)) {
              nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[rstart_next + gbstep * i / abs(factors[step - 1]) + rank % (gbstep / abs(factors[step - 1]))] - ranks[rank]) % num_ranks);
	    }
	  }
	}
      } else if (abs(factors[step - 1]) != factors[step]) {
	for (i = 0; i < abs(factors[step - 1]); i++) {
	  if (rank != rstart_next + gbstep * i / abs(factors[step - 1])) {
	    if (flag && (expart == 0 || expart == 2)) {
              nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[rstart_next + gbstep * i / abs(factors[step - 1])] - ranks[rank]) % num_ranks);
	    }
	  }
	}
      }
      for (substep = 0; substep < factors[step]; substep++) {
	if ((rank - rstart) / gbstep == substep) substep++;
	if (substep < factors[step]) {
          offset = displs[(rank / gbstep) * gbstep] + offset_global;
	  size_local = 0;
          for (i = 0; i < gbstep; i++) {
            size_local += sizes[(rank / gbstep) * gbstep + i];
          }
          if (size_local > 0 && (expart == 0 || expart == 2)) {
            nbuffer_out += write_memcpy_reduce(esmemcpy, erecvbufp, (num_ranks + ranks[(rank + (socket_size - rank + rstart + gbstep * substep + rank % gbstep) % socket_size) % socket_size] - ranks[rank]) % num_ranks, 0, offset, erecvbufp, 0, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
	  }
        }
      }
    }
    for (i = 0; i < factors[step - 1]; i++) {
      if (rank != gbstep * i / factors[step - 1] + rank % (gbstep / factors[step - 1])) {
	if (expart == 0 || expart == 2) {
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[gbstep * i / factors[step - 1] + rank % (gbstep / factors[step - 1])] - ranks[rank]) % num_ranks);
	}
      }
    }
  } else {
    for (i = 1; i < socket_size; i++) {
      offset = displs[(socket_size + i + rank) % socket_size] + offset_global;
      size_local = sizes[(socket_size + i + rank) % socket_size];
      if (size_local > 0 && (expart == 0 || expart == 2)) {
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "sd", ewait_socket_barrier, (num_ranks + ranks[(rank + i) % socket_size] - ranks[rank]) % num_ranks);
        nbuffer_out += write_memcpy_reduce(esmemcpy, erecvbufp, 0, 0, offset, erecvbufp, (num_ranks + ranks[(rank + i) % socket_size] - ranks[rank]) % num_ranks, 0, offset, size_local, 0, buffer_out + nbuffer_out, ascii);
      }
    }
    if (expart == 0 || expart == 2) {
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, ascii, "s", esocket_barrier);
    }
  }
  return nbuffer_out;
}

int reduce_copies_one_socket(int copyin_method, int socket_size, int num_factors, int *factors, int add, int size, int type_size, int rank, int num_ranks, int *ranks, int first_call, char *buffer_out, int ascii) {
  int nbuffer_out = 0, size_local, add_local, num_factors_loc, socket_size_loc, ranks_loc[num_ranks], rank_loc = -1, ranks_loci[num_ranks], rank_loci = -1, sizes[socket_size], displs[socket_size], i;
  for (num_factors_loc = 1; num_factors_loc < num_factors && factors[num_factors_loc] != 1; num_factors_loc++);
  if (num_factors_loc != num_factors) {
    socket_size_loc = factors[num_factors_loc + 1];
    for (i = 0; i < socket_size_loc; i++){
      ranks_loc[i] = ranks[i * socket_size / socket_size_loc + rank % (socket_size / socket_size_loc)];
    }
  } else {
    socket_size_loc = socket_size;
    for (i = 0; i < socket_size; i++){
      ranks_loc[i] = ranks[i];
    }
  }
  for (i = 0; i < socket_size_loc; i++) {
    if (ranks_loc[i] == ranks[rank]) {
      rank_loc = i;
      break;
    }
  }
  if (factors[0] < 0) {
//    if (type_size * abs(factors[0]) >= size) {
    if (0) {
      nbuffer_out += reduce_copies_redundant(socket_size_loc, num_factors_loc - 1, factors + 1, size, type_size, rank_loc, num_ranks, ranks_loc, add, 1, first_call, buffer_out + nbuffer_out, ascii);
    } else {
      if (copyin_method == 5) {
        nbuffer_out += reduce_copies_cyclic(socket_size_loc, num_factors_loc - 1, factors + 1, size, type_size, rank_loc, num_ranks, ranks_loc, -factors[0], add, 1, first_call, buffer_out + nbuffer_out, ascii);
      } else {
        nbuffer_out += reduce_copies_recursive(socket_size_loc, num_factors_loc - 1, factors + 1, size, type_size, rank_loc, num_ranks, ranks_loc, -factors[0], add, 1, first_call, buffer_out + nbuffer_out, ascii);
      }
      if (num_factors_loc != num_factors) {
        for (i = 0; i < socket_size / socket_size_loc; i++){
          ranks_loci[i] = ranks[i + rank / (socket_size / socket_size_loc) * (socket_size / socket_size_loc)];
        }
        for (i = 0; i < socket_size / socket_size_loc; i++) {
	  if (ranks_loci[i] == ranks[rank]) {
	    rank_loci = i;
	    break;
	  }
        }
        sizes_displs(socket_size_loc, size, type_size, -factors[0], sizes, displs);
        nbuffer_out += reduce_copies_one_socket(copyin_method, socket_size / socket_size_loc, num_factors - num_factors_loc - 2, factors + num_factors_loc + 2, add + displs[rank_loc], sizes[rank_loc], type_size, rank_loci, num_ranks, ranks_loci, 0, buffer_out + nbuffer_out, ascii);
      }
      if (copyin_method == 5) {
        nbuffer_out += reduce_copies_cyclic(socket_size_loc, num_factors_loc - 1, factors + 1, size, type_size, rank_loc, num_ranks, ranks_loc, -factors[0], add, 2, 0, buffer_out + nbuffer_out, ascii);
      } else {
        nbuffer_out += reduce_copies_recursive(socket_size_loc, num_factors_loc - 1, factors + 1, size, type_size, rank_loc, num_ranks, ranks_loc, -factors[0], add, 2, 0, buffer_out + nbuffer_out, ascii);
      }
    }
  } else {
    for (i = 0; i < factors[0]; i++) {
      size_local = (size / type_size) / factors[0];
      if (i >= (size / type_size) % factors[0]) {
        add_local = size_local * i + (size / type_size) % factors[0];
      } else {
        add_local = size_local * i + i;
        size_local++;
      }
      size_local *= type_size;
      add_local *= type_size;
      if (size_local > 0 || socket_size != num_ranks) {
        if (copyin_method == 5) {
	  nbuffer_out += reduce_copies_cyclic(socket_size_loc, num_factors_loc - 1, factors + 1, size_local, type_size, rank_loc, num_ranks, ranks_loc, 0, add + add_local, 1, first_call, buffer_out + nbuffer_out, ascii);
        } else {
	  nbuffer_out += reduce_copies_recursive(socket_size_loc, num_factors_loc - 1, factors + 1, size_local, type_size, rank_loc, num_ranks, ranks_loc, 0, add + add_local, 1, first_call, buffer_out + nbuffer_out, ascii);
	}
        if (num_factors_loc != num_factors) {
          for (i = 0; i < socket_size / socket_size_loc; i++){
            ranks_loci[i] = ranks[i + rank / (socket_size / socket_size_loc) * (socket_size / socket_size_loc)];
          }
          for (i = 0; i < socket_size / socket_size_loc; i++) {
	    if (ranks_loci[i] == ranks[rank]) {
	      rank_loci = i;
	      break;
	    }
          }
          sizes_displs(socket_size_loc, size_local, type_size, 0, sizes, displs);
          nbuffer_out += reduce_copies_one_socket(copyin_method, socket_size / socket_size_loc, num_factors - num_factors_loc - 2, factors + num_factors_loc + 2, add + add_local + displs[rank_loc], sizes[rank_loc], type_size, rank_loci, num_ranks, ranks_loci, 0, buffer_out + nbuffer_out, ascii);
        }
        if (copyin_method == 5) {
	  nbuffer_out += reduce_copies_cyclic(socket_size_loc, num_factors_loc - 1, factors + 1, size_local, type_size, rank_loc, num_ranks, ranks_loc, 0, add + add_local, 2, 0, buffer_out + nbuffer_out, ascii);
        } else {
	  nbuffer_out += reduce_copies_recursive(socket_size_loc, num_factors_loc - 1, factors + 1, size_local, type_size, rank_loc, num_ranks, ranks_loc, 0, add + add_local, 2, 0, buffer_out + nbuffer_out, ascii);
	}
      }
    }
  }
  return nbuffer_out;
}

int ext_mpi_generate_reduce_copyin(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce;
  int num_nodes = 1, size, add, add2, node_rank, node_row_size = 1,
      node_column_size = 1, node_size, *counts = NULL, counts_max = 0,
      *displs = NULL, *iocounts = NULL, iocounts_max = 0, *iodispls = NULL,
      *lcounts = NULL, *ldispls = NULL, lrank_row, lrank_column, instance,
      nbuffer_out = 0, nbuffer_in = 0, *mcounts = NULL, *moffsets = NULL, i, j,
      k, *ranks, gbstep, collective_type = 1, fast,
      type_size = 1, num_ranks, num_factors, *factors, instance_max;
  struct data_algorithm data;
  struct parameters_block *parameters;
  data.num_blocks = 0;
  data.blocks = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
//  parameters->node /= (parameters->num_nodes / parameters->message_sizes_max);
//  parameters->num_nodes = parameters->message_sizes_max;
  num_nodes = parameters->num_sockets;
  node_rank = parameters->socket_rank;
  node_row_size = parameters->socket_row_size;
  node_column_size = parameters->socket_column_size;
  num_factors = parameters->copyin_factors_max;
  factors = parameters->copyin_factors;
  if (parameters->collective_type == collective_type_allgatherv) {
    collective_type = 0;
  }
  if (parameters->collective_type == collective_type_reduce_scatter) {
    collective_type = 2;
  }
  mcounts = parameters->message_sizes;
  counts_max = parameters->counts_max;
  counts = parameters->counts;
  iocounts_max = parameters->iocounts_max;
  iocounts = parameters->iocounts;
  switch (parameters->data_type) {
  case data_type_char:
    type_size = sizeof(char);
    break;
  case data_type_int:
    type_size = sizeof(int);
    break;
  case data_type_float:
    type_size = sizeof(float);
    break;
  case data_type_long_int:
    type_size = sizeof(long int);
    break;
  case data_type_double:
    type_size = sizeof(double);
    break;
  }
  moffsets = (int *)malloc((num_nodes + 1) * sizeof(int));
  if (!moffsets)
    goto error;
  node_size = node_row_size * node_column_size;
  nbuffer_in += i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm reduce_copyin\n");
    exit(2);
  }
  nbuffer_out += ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  displs = (int *)malloc((counts_max + 1) * sizeof(int));
  if (!displs)
    goto error;
  displs[0] = 0;
  for (i = 0; i < counts_max; i++) {
    displs[i + 1] = displs[i] + counts[i];
  }
  iodispls = (int *)malloc((iocounts_max + 1) * sizeof(int));
  if (!iodispls)
    goto error;
  iodispls[0] = 0;
  for (i = 0; i < iocounts_max; i++) {
    iodispls[i + 1] = iodispls[i] + iocounts[i];
  }
  lrank_row = node_rank / counts_max;
  lrank_column = node_rank % counts_max;
  lcounts = (int *)malloc(sizeof(int) * (node_size / counts_max));
  if (!lcounts)
    goto error;
  ldispls = (int *)malloc(sizeof(int) * (node_size / counts_max + 1));
  if (!ldispls)
    goto error;
  for (i = 0; i < node_size / counts_max; i++) {
    lcounts[i] = (counts[lrank_column] / type_size) / (node_size / counts_max);
    if (i < (counts[lrank_column] / type_size) % (node_size / counts_max)) {
      lcounts[i]++;
    }
    lcounts[i] *= type_size;
  }
  ldispls[0] = 0;
  for (i = 0; i < node_size / counts_max; i++) {
    ldispls[i + 1] = ldispls[i] + lcounts[i];
  }
  moffsets[0] = 0;
  for (i = 0; i < num_nodes; i++) {
    moffsets[i + 1] = moffsets[i] + mcounts[i];
  }
  if ((data.blocks[0].num_lines == 1) && (collective_type == 0)) {
    for (i = 0; i < num_nodes; i++) {
      moffsets[i] = moffsets[num_nodes];
    }
  }
  nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
  if (collective_type) {
    if (((collective_type == 2) && (parameters->root >= 0)) ||
        (parameters->root <= -10)) {
      if ((parameters->socket * parameters->socket_row_size +
               parameters->socket_rank ==
           parameters->root) ||
          (parameters->socket * parameters->socket_row_size +
               parameters->socket_rank ==
           -10 - parameters->root)) {
        add = 0;
        for (i = 0; i < num_nodes; i++) {
          j = (num_nodes + i + parameters->socket) % num_nodes;
          data_memcpy_reduce.type = ememcpy;
          data_memcpy_reduce.buffer_type1 = eshmemo;
          data_memcpy_reduce.buffer_number1 = 0;
          data_memcpy_reduce.is_offset1 = 0;
          data_memcpy_reduce.offset1 = add;
          data_memcpy_reduce.buffer_type2 = esendbufp;
          data_memcpy_reduce.buffer_number2 = 0;
          data_memcpy_reduce.is_offset2 = 0;
          data_memcpy_reduce.offset2 = moffsets[j];
          data_memcpy_reduce.size = mcounts[j];
          nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
          add += mcounts[j];
        }
      }
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
    } else {
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
      switch (parameters->copyin_method) {
      case 5:
      case 6:
	ranks = (int*)malloc(node_size * sizeof(int));
	for (i = 0; i < node_size; i++) {
	   ranks[i] = i;
	}
	if (parameters->copyin_method == 6) {
	  for (i = 1; factors[i] < 0 && i < num_factors; i++);
	  rank_order(node_size, i - 1, factors + 1, ranks);
	  for (i = 0; i < node_size; i++) {
	    if (ranks[i] == lrank_row) {
	      lrank_row = i;
	      break;
	    }
	  }
	}
	nbuffer_out += reduce_copies_one_socket(parameters->copyin_method, node_size, num_factors, factors, 0, moffsets[num_nodes], type_size, lrank_row, node_size, ranks, !parameters->in_place, buffer_out + nbuffer_out, parameters->ascii_out);
	free(ranks);
	break;
      case 0:
      case 1:
        ranks = (int*)malloc(sizeof(int) * node_size);
        gbstep = factors[0];
        if (node_size % gbstep == 0 || lrank_row / gbstep < (node_size - 1) / gbstep) {
          num_ranks = gbstep;
        } else {
	  num_ranks = node_size % gbstep;
        }
        for (j = 0; j < num_ranks; j++) {
          ranks[(num_ranks + j - lrank_row % gbstep) % num_ranks] = (lrank_row / gbstep) * gbstep + j % num_ranks;
        }
        instance = lrank_row / gbstep;
        for (i = 0, j = 1; i < num_factors; j *= factors[i++])
          ;
        instance_max = (j - 1) / gbstep;
        fast = !parameters->on_gpu && parameters->num_sockets == 1 && parameters->message_sizes[0] <= CACHE_LINE_SIZE - OFFSET_FAST;
        if (fast) {
          type_size = moffsets[num_nodes];
        }
        if (parameters->copyin_method == 1) {
          for (i = 1; i < node_size / 1; i *= 2)
	    ;
	  num_ranks = gbstep = factors[0];
          instance = lrank_row / gbstep;
          instance_max = (node_size - 1) / gbstep;
          nbuffer_out += reduce_copyin(&data, num_nodes, counts_max, mcounts, moffsets, ldispls, lrank_row % gbstep, lrank_column, type_size, instance, instance_max, gbstep, num_ranks, ranks, fast, 0, buffer_out + nbuffer_out, parameters->ascii_out);
          nbuffer_out += reduce_copies_almost_half(node_size, i / 2, moffsets[num_nodes], type_size, lrank_row, fast, buffer_out + nbuffer_out, parameters->ascii_out);
          nbuffer_out += reduce_copies(i / 2, num_factors - 1, factors, moffsets[num_nodes], type_size, lrank_row, fast, 1, buffer_out + nbuffer_out, parameters->ascii_out);
        } else {
          nbuffer_out += reduce_copyin(&data, num_nodes, counts_max, mcounts, moffsets, ldispls, lrank_row % gbstep, lrank_column, type_size, instance, instance_max, gbstep, num_ranks, ranks, fast, 0, buffer_out + nbuffer_out, parameters->ascii_out);
          nbuffer_out += reduce_copies(node_size, num_factors, factors, moffsets[num_nodes], type_size, lrank_row, fast, 0, buffer_out + nbuffer_out, parameters->ascii_out);
        }
        free(ranks);
	break;
      }
//      nbuffer_out += reduce_copies_big((node_size - 1) / factors[0] + 1, node_size, node_size, moffsets[num_nodes], type_size, lrank_row, fast, buffer_out + nbuffer_out, parameters->ascii_out);
    }
  } else {
    add = iodispls[node_rank];
    k = 1;
    for (i = 0; (i < data.blocks[0].num_lines) && k; i++) {
      for (j = 0; j < data.blocks[0].lines[i].recvfrom_max; j++){
        if (data.blocks[0].lines[i].recvfrom_node[j] == -1){
          k = 0;
        }
      }
      if (k){
        size = mcounts[data.blocks[0].lines[i].frac];
        add += size;
      }
    }
    add2 = 0;
    size = iocounts[node_rank];
    if (size) {
      data_memcpy_reduce.type = ememcpy;
      data_memcpy_reduce.buffer_type1 = eshmemo;
      data_memcpy_reduce.buffer_number1 = 0;
      data_memcpy_reduce.is_offset1 = 0;
      data_memcpy_reduce.offset1 = add;
      data_memcpy_reduce.buffer_type2 = esendbufp;
      data_memcpy_reduce.buffer_number2 = 0;
      data_memcpy_reduce.is_offset2 = 0;
      data_memcpy_reduce.offset2 = add2;
      data_memcpy_reduce.size = size;
      nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
    }
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
  }
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(data);
  free(ldispls);
  free(lcounts);
  free(iodispls);
  free(displs);
  free(moffsets);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(data);
  free(ldispls);
  free(lcounts);
  free(iodispls);
  free(displs);
  free(moffsets);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
