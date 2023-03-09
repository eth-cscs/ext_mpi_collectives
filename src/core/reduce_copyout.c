#include "reduce_copyout.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int overlapp(int dest_start, int dest_end, int source_start,
                    int source_end, int *add) {
  int start, end;
  start = dest_start;
  if (source_start > start)
    start = source_start;
  end = dest_end;
  if (source_end < end)
    end = source_end;
  *add = start;
  if (end - start > 0) {
    return (end - start);
  } else {
    return (0);
  }
}

int ext_mpi_generate_reduce_copyout(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce;
  int num_nodes = 1, size, add, add2, node_rank, node_row_size = 1,
      node_column_size = 1, node_size, *counts = NULL, counts_max = 0,
      *iocounts = NULL, iocounts_max = 0, *displs = NULL, *iodispls = NULL,
      *lcounts = NULL, *ldispls = NULL, lrank_column, fast = 0,
      nbuffer_out = 0, nbuffer_in = 0, *mcounts = NULL, *moffsets = NULL, i, j,
      k, allreduce = 1, flag, type_size = 1;
  struct data_algorithm data;
  struct parameters_block *parameters;
  char cline[1000];
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
  // FIXME
  //  node_row_size=parameters->node_row_size;
  //  node_column_size=parameters->node_column_size;
  if (parameters->collective_type == collective_type_reduce_scatter) {
    allreduce = 0;
  }
  mcounts = parameters->message_sizes;
  counts_max = parameters->counts_max;
  counts = parameters->counts;
  iocounts_max = parameters->iocounts_max;
  iocounts = parameters->iocounts;
  if (!parameters->on_gpu && parameters->num_sockets == 1 && parameters->message_sizes[0] <= CACHE_LINE_SIZE - OFFSET_FAST) {
    fast = 1;
  }
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
    printf("error reading algorithm reduce_copyout\n");
    exit(2);
  }
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, cline, parameters->ascii_in);
    if (flag > 0) {
      nbuffer_out +=
          ext_mpi_write_line(buffer_out + nbuffer_out, cline, parameters->ascii_out);
    }
  } while (flag);
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
  lrank_column = node_rank % counts_max;
  lcounts = (int *)malloc(sizeof(int) * (node_size / counts_max));
  if (!lcounts)
    goto error;
  ldispls = (int *)malloc(sizeof(int) * (node_size / counts_max + 1));
  for (i = 0; i < node_size / counts_max; i++) {
    lcounts[i] = (counts[lrank_column] / type_size) / (node_size / counts_max);
    if (lrank_column <
        (counts[lrank_column] / type_size) % (node_size / counts_max)) {
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
  if (allreduce) {
    if ((parameters->root == -1) ||
        ((parameters->root < 0) &&
         (-10 - parameters->root !=
          parameters->socket * parameters->socket_row_size +
              parameters->socket_rank % parameters->socket_row_size)) ||
        (parameters->root ==
         parameters->socket * parameters->socket_row_size +
             parameters->socket_rank % parameters->socket_row_size)) {
      add2 = 0;
      for (i = 0; i < data.blocks[data.num_blocks - 1].num_lines; i++) {
        k = 0;
        for (j = 0; j < data.blocks[data.num_blocks - 1].lines[i].sendto_max; j++) {
          if (data.blocks[data.num_blocks - 1].lines[i].sendto_node[j] == -1) {
            k = 1;
          }
        }
        if (k) {
          if ((size = overlapp(ldispls[lrank_column], ldispls[lrank_column + 1],
                               moffsets[data.blocks[data.num_blocks - 1].lines[i].frac],
                               moffsets[data.blocks[data.num_blocks - 1].lines[i].frac + 1],
                               &add))) {
            data_memcpy_reduce.type = ememcpy;
            data_memcpy_reduce.buffer_type1 = erecvbufp;
            data_memcpy_reduce.is_offset1 = 0;
            data_memcpy_reduce.offset1 = add;
            data_memcpy_reduce.buffer_type2 = eshmemo;
            data_memcpy_reduce.buffer_number2 = 0;
	    if (!fast) {
              data_memcpy_reduce.is_offset2 = 0;
              data_memcpy_reduce.offset2 = add2;
	    } else {
              data_memcpy_reduce.is_offset2 = 1;
              data_memcpy_reduce.offset_number2 = -1;
              data_memcpy_reduce.offset2 = add2 + sizeof(long int);
	    }
            data_memcpy_reduce.size = size;
            nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
          }
        }
        add2 += mcounts[data.blocks[data.num_blocks - 1].lines[i].frac];
      }
    }
  } else {
    add = 0;
    add2 = iodispls[node_rank];
    size = iocounts[node_rank];
    for (i = 0; i < data.blocks[data.num_blocks - 1].num_lines; i++) {
      if (data.blocks[data.num_blocks - 1].lines[i].sendto_max > 0) {
        break;
      }
      add2 += mcounts[data.blocks[data.num_blocks - 1].lines[i].frac];
    }
    if (size) {
      data_memcpy_reduce.type = ememcpy;
      data_memcpy_reduce.buffer_type1 = erecvbufp;
      data_memcpy_reduce.is_offset1 = 0;
      data_memcpy_reduce.offset1 = add;
      data_memcpy_reduce.buffer_type2 = eshmemo;
      data_memcpy_reduce.buffer_number2 = 0;
      data_memcpy_reduce.is_offset2 = 0;
      data_memcpy_reduce.offset2 = add2;
      data_memcpy_reduce.size = size;
      nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
    }
  }
  nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", ereturn);
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
