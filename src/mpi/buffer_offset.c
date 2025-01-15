#include "buffer_offset.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_buffer_offset(char *buffer_in, char *buffer_out, MPI_Comm *comm) {
  struct line_memcpy_reduce data_memcpy_reduce;
  struct line_irecv_isend data_irecv_isend;
  int nbuffer_out = 0, nbuffer_in = 0, nbuffer_in2 = 0, o1,
      buffer_offset_max = -1, locmem_max = 0, flag, i, j;
  long int **buffer_offset = NULL, **buffer_offset2 = NULL, *shmem_max = NULL;
  char line[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  shmem_max = (long int*)malloc(parameters->socket_row_size * parameters->num_sockets_per_node * sizeof(long int));
  memset(shmem_max, 0, parameters->socket_row_size * parameters->num_sockets_per_node * sizeof(long int));
  buffer_in += nbuffer_in;
  do {
    nbuffer_in2 += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in2, line, parameters->ascii_in);
    if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
      if (data_memcpy_reduce.type == ememcpy || data_memcpy_reduce.type == ereduce ||
          data_memcpy_reduce.type == esmemcpy || data_memcpy_reduce.type == esreduce ||
	  data_memcpy_reduce.type == ememcp_ || data_memcpy_reduce.type == ereduc_ ||
          data_memcpy_reduce.type == esmemcp_ || data_memcpy_reduce.type == esreduc_) {    
        if (data_memcpy_reduce.buffer_type1 == eshmemo && data_memcpy_reduce.is_offset1 && data_memcpy_reduce.offset_number1 > buffer_offset_max) {
          buffer_offset_max = data_memcpy_reduce.offset_number1;
        }
        if (data_memcpy_reduce.buffer_type2 == eshmemo && data_memcpy_reduce.is_offset2 && data_memcpy_reduce.offset_number2 > buffer_offset_max) {
          buffer_offset_max = data_memcpy_reduce.offset_number2;
        }
      }
    } else if (ext_mpi_read_irecv_isend(line, &data_irecv_isend) >= 0) {
      if (data_irecv_isend.type == eisend || data_irecv_isend.type == eirecv ||
          data_irecv_isend.type == eisen_ || data_irecv_isend.type == eirec_) {
	if (data_irecv_isend.buffer_type == eshmemo && data_irecv_isend.is_offset && data_irecv_isend.offset_number > buffer_offset_max) {
	  buffer_offset_max = data_irecv_isend.offset_number;
	}
      }
    }
  } while (flag > 0);
  buffer_offset_max++;
  if (comm) {
    PMPI_Allreduce(MPI_IN_PLACE, &buffer_offset_max, 1, MPI_INT, MPI_MAX, *comm);
  }
  buffer_offset = (long int **)malloc(sizeof(long int*) * parameters->socket_row_size * parameters->num_sockets_per_node);
  if (!buffer_offset)
    goto error;
  buffer_offset2 = (long int **)malloc(sizeof(long int*) * parameters->socket_row_size * parameters->num_sockets_per_node);
  if (!buffer_offset2)
    goto error;
  for (i = 0; i < parameters->socket_row_size; i++) {
    buffer_offset[i] = (long int *)malloc(sizeof(long int) * buffer_offset_max);
    if (!buffer_offset[i])
      goto error;
    buffer_offset2[i] = (long int *)malloc(sizeof(long int) * buffer_offset_max);
    if (!buffer_offset2[i])
      goto error;
    memset(buffer_offset[i], 0, sizeof(long int) * buffer_offset_max);
  }
  nbuffer_in2 = 0;
  if (buffer_offset_max >= 0) {
    do {
      nbuffer_in2 += flag =
          ext_mpi_read_line(buffer_in + nbuffer_in2, line, parameters->ascii_in);
      if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0 &&
        (data_memcpy_reduce.type == ememcpy || data_memcpy_reduce.type == ereduce ||
            data_memcpy_reduce.type == esmemcpy || data_memcpy_reduce.type == esreduce)) {
        if (data_memcpy_reduce.buffer_type1 == eshmemo && data_memcpy_reduce.is_offset1 && data_memcpy_reduce.offset_number1 >= 0 && ((long int)data_memcpy_reduce.offset1 + (long int)data_memcpy_reduce.size > buffer_offset[data_memcpy_reduce.buffer_number1][data_memcpy_reduce.offset_number1])) {
          buffer_offset[data_memcpy_reduce.buffer_number1][data_memcpy_reduce.offset_number1] = (long int)data_memcpy_reduce.offset1 + (long int)data_memcpy_reduce.size;
        } else if (data_memcpy_reduce.buffer_type1 == eshmemo && !data_memcpy_reduce.is_offset1 && ((long int)data_memcpy_reduce.offset1 + (long int)data_memcpy_reduce.size > shmem_max[data_memcpy_reduce.buffer_number1])) {
          shmem_max[data_memcpy_reduce.buffer_number1] = (long int)data_memcpy_reduce.offset1 + (long int)data_memcpy_reduce.size;
        }
        if (data_memcpy_reduce.buffer_type2 == eshmemo && data_memcpy_reduce.is_offset2 && data_memcpy_reduce.offset_number2 >= 0 && ((long int)data_memcpy_reduce.offset2 + (long int)data_memcpy_reduce.size > buffer_offset[data_memcpy_reduce.buffer_number2][data_memcpy_reduce.offset_number2])) {
          buffer_offset[data_memcpy_reduce.buffer_number2][data_memcpy_reduce.offset_number2] = (long int)data_memcpy_reduce.offset2 + (long int)data_memcpy_reduce.size;
        } else if (data_memcpy_reduce.buffer_type2 == eshmemo && !data_memcpy_reduce.is_offset2 && ((long int)data_memcpy_reduce.offset2 + (long int)data_memcpy_reduce.size > shmem_max[data_memcpy_reduce.buffer_number2])) {
          shmem_max[data_memcpy_reduce.buffer_number2] = (long int)data_memcpy_reduce.offset2 + (long int)data_memcpy_reduce.size;
        }
      } else if ((data_memcpy_reduce.type == ewaitall) || (data_memcpy_reduce.type == ewaitany)) {
          ext_mpi_read_assembler_line(line, 0, "sd", &estring1, &o1, 0);
        if (o1 > locmem_max) {
          locmem_max = o1;
        }
      } else if (ext_mpi_read_irecv_isend(line, &data_irecv_isend) >= 0 &&
          (data_irecv_isend.type == eisend || data_irecv_isend.type == eirecv)) {
        if (data_irecv_isend.buffer_type == eshmemo && data_irecv_isend.is_offset && data_irecv_isend.offset_number >= 0 && ((long int)data_irecv_isend.offset + (long int)data_irecv_isend.size > buffer_offset[data_irecv_isend.buffer_number][data_irecv_isend.offset_number])) {
          buffer_offset[data_irecv_isend.buffer_number][data_irecv_isend.offset_number] = (long int)data_irecv_isend.offset + (long int)data_irecv_isend.size;
        } else if (data_irecv_isend.buffer_type == eshmemo && !data_irecv_isend.is_offset && ((long int)data_irecv_isend.offset + (long int)data_irecv_isend.size > shmem_max[data_irecv_isend.buffer_number])) {
          shmem_max[data_irecv_isend.buffer_number] = (long int)data_irecv_isend.offset + (long int)data_irecv_isend.size;
        }
      }
    } while (flag > 0);
  }
  parameters->locmem_max = locmem_max;
  parameters->shmem_buffer_offset_max = buffer_offset_max * parameters->socket_row_size;
  if (comm) {
    for (i = 0; i < parameters->socket_row_size; i++) {
      for (j = 0; j < parameters->shmem_buffer_offset_max / parameters->socket_row_size; j++) {
        PMPI_Allreduce(MPI_IN_PLACE, &buffer_offset[(parameters->socket_row_size + i - parameters->socket_rank) % parameters->socket_row_size][j], 1, MPI_LONG, MPI_MAX, *comm);
      }
      PMPI_Allreduce(MPI_IN_PLACE, &shmem_max[(parameters->socket_row_size + i - parameters->socket_rank) % parameters->socket_row_size], 1, MPI_LONG, MPI_MAX, *comm);
    }
  }
  if (buffer_offset_max) {
    for (j = 0; j < parameters->socket_row_size; j++) {
      buffer_offset2[j][0] = 0;
      for (i = 1; i < buffer_offset_max; i++) {
        if (buffer_offset[j][i - 1] >= CACHE_LINE_SIZE) {
          buffer_offset2[j][i] = buffer_offset2[j][i - 1] + buffer_offset[j][i - 1];
        } else if (buffer_offset[j][i - 1] > 0) {
          buffer_offset2[j][i] = buffer_offset2[j][i - 1] + CACHE_LINE_SIZE;
        } else {
          buffer_offset2[j][i] = buffer_offset2[j][i - 1];
        }
      }
      if (buffer_offset2[j][buffer_offset_max - 1]) {
        buffer_offset2[j][buffer_offset_max - 1] = ((buffer_offset2[j][buffer_offset_max - 1] / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
      }
      if (shmem_max[j] > buffer_offset2[j][buffer_offset_max - 1]) {
        buffer_offset2[j][buffer_offset_max - 1] = shmem_max[j];
      }
      if (shmem_max[j] < buffer_offset2[j][buffer_offset_max - 1]) {
        shmem_max[j] = buffer_offset2[j][buffer_offset_max - 1];
      }
    }
  }
  nbuffer_in2 = 0;
  free(parameters->shmem_buffer_offset);
  parameters->shmem_buffer_offset = (int*)malloc(sizeof(int) * parameters->shmem_buffer_offset_max);
  if (!parameters->shmem_buffer_offset)
    goto error;
  for (i = 0; i < parameters->socket_row_size; i++) {
    for (j = 0; j < parameters->shmem_buffer_offset_max / parameters->socket_row_size; j++) {
      parameters->shmem_buffer_offset[j + parameters->shmem_buffer_offset_max / parameters->socket_row_size * i] = buffer_offset2[i][j];
      if (parameters->shmem_buffer_offset[j + parameters->shmem_buffer_offset_max / parameters->socket_row_size * i] != buffer_offset2[i][j]) {
        goto error;
      }
    }
  }
  parameters->shmem_max = shmem_max[0];
  if (parameters->copyin_method == 7 && parameters->num_sockets_per_node > 1) {
    for (i = 0; i < parameters->counts_max; i++) {
      parameters->shmem_max += parameters->counts[i];
    }
  }
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in2 += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in2, line, parameters->ascii_in);
    if (flag) {
      nbuffer_out +=
          ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
    }
  } while (flag > 0);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  free(shmem_max);
  for (i = 0; i < parameters->socket_row_size; i++) {
    free(buffer_offset2[i]);
    free(buffer_offset[i]);
  }
  free(buffer_offset2);
  free(buffer_offset);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  free(shmem_max);
  for (i = 0; i < parameters->socket_row_size; i++) {
    free(buffer_offset2[i]);
    free(buffer_offset[i]);
  }
  free(buffer_offset2);
  free(buffer_offset);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
