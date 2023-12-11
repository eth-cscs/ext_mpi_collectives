#include "reduce_scatter_single_node.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

int ext_mpi_generate_reduce_scatter_single_node(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce;
  int *moffsets = NULL, i, nbuffer_in = 0, nbuffer_out = 0;
  struct data_algorithm data;
  struct parameters_block *parameters;
  data.num_blocks = 0;
  data.blocks = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_in += i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm reduce_copyin\n");
    exit(2);
  }
//  nbuffer_out += ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  moffsets = (int *)malloc((parameters->socket_row_size + 1) * sizeof(int));
  if (!moffsets)
    goto error;
  moffsets[0] = 0;
  for (i = 0; i < parameters->socket_row_size; i++) {
    moffsets[i + 1] = moffsets[i] + parameters->iocounts[i];
  }
  nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
  data_memcpy_reduce.type = ememcpy;
  data_memcpy_reduce.buffer_type1 = eshmemo;
  data_memcpy_reduce.buffer_number1 = 0;
  data_memcpy_reduce.is_offset1 = 0;
  data_memcpy_reduce.offset1 = moffsets[parameters->socket_row_size] * parameters->socket_rank;
  data_memcpy_reduce.buffer_type2 = esendbufp;
  data_memcpy_reduce.buffer_number2 = 0;
  data_memcpy_reduce.is_offset2 = 0;
  data_memcpy_reduce.offset2 = 0;
  data_memcpy_reduce.size = moffsets[parameters->socket_row_size];
  nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
  data_memcpy_reduce.type = ememcp_;
  data_memcpy_reduce.buffer_type1 = eshmemo;
  data_memcpy_reduce.buffer_number1 = 0;
  data_memcpy_reduce.is_offset1 = 0;
  data_memcpy_reduce.offset1 = moffsets[parameters->socket_row_size] * (parameters->socket_row_size - 1);
  data_memcpy_reduce.buffer_type2 = esendbufp;
  data_memcpy_reduce.buffer_number2 = 0;
  data_memcpy_reduce.is_offset2 = 0;
  data_memcpy_reduce.offset2 = 0;
  data_memcpy_reduce.size = moffsets[parameters->socket_row_size];
  nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
  nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
  data_memcpy_reduce.type = ememcpy;
  data_memcpy_reduce.buffer_type1 = erecvbufp;
  data_memcpy_reduce.buffer_number1 = 0;
  data_memcpy_reduce.is_offset1 = 0;
  data_memcpy_reduce.offset1 = 0;
  data_memcpy_reduce.buffer_type2 = eshmemo;
  data_memcpy_reduce.buffer_number2 = 0;
  data_memcpy_reduce.is_offset2 = 0;
  data_memcpy_reduce.offset2 = moffsets[parameters->socket_rank];
  data_memcpy_reduce.size = parameters->iocounts[parameters->socket_rank];
  nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
  for (i = 1; i < parameters->socket_row_size; i++) {
    data_memcpy_reduce.type = ereduce;
    data_memcpy_reduce.buffer_type1 = erecvbufp;
    data_memcpy_reduce.buffer_number1 = 0;
    data_memcpy_reduce.is_offset1 = 0;
    data_memcpy_reduce.offset1 = 0;
    data_memcpy_reduce.buffer_type2 = eshmemo;
    data_memcpy_reduce.buffer_number2 = 0;
    data_memcpy_reduce.is_offset2 = 0;
    data_memcpy_reduce.offset2 = moffsets[parameters->socket_row_size] * i + moffsets[parameters->socket_rank];
    data_memcpy_reduce.size = parameters->iocounts[parameters->socket_rank];
    nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
  }
  nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", ereturn);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(data);
  free(moffsets);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(data);
  free(moffsets);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
