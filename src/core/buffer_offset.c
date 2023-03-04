#include "buffer_offset.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_buffer_offset(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce;
  int nbuffer_out = 0, nbuffer_in = 0, nbuffer_in2 = 0, o1,
      buffer_offset_max = 0, locmem_max = 0, flag, i;
  long int *buffer_offset = NULL, *buffer_offset2 = NULL, shmem_max = 0;
  char line[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  buffer_in += nbuffer_in;
  do {
    nbuffer_in2 += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in2, line, parameters->ascii_in);
    if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0){
      if ((data_memcpy_reduce.type == ememcpy) || (data_memcpy_reduce.type == ereduce) ||
          (data_memcpy_reduce.type == ememcp_) || (data_memcpy_reduce.type == ereduc_) ||
          (data_memcpy_reduce.type == esmemcpy) || (data_memcpy_reduce.type == esmemcp_) ||
          (data_memcpy_reduce.type == esreduce) || (data_memcpy_reduce.type == esreduc_)) {
        if (data_memcpy_reduce.is_offset1 && data_memcpy_reduce.offset_number1 > buffer_offset_max) {
          buffer_offset_max = data_memcpy_reduce.offset_number1;
        }
        if (data_memcpy_reduce.is_offset2 && data_memcpy_reduce.offset_number2 > buffer_offset_max) {
          buffer_offset_max = data_memcpy_reduce.offset_number2;
        }
      }
    }
  } while (flag > 0);
  buffer_offset_max += 2;
  buffer_offset = (long int *)malloc(sizeof(long int) * buffer_offset_max);
  if (!buffer_offset)
    goto error;
  buffer_offset2 = (long int *)malloc(sizeof(long int) * buffer_offset_max);
  if (!buffer_offset2)
    goto error;
  for (i = 0; i < buffer_offset_max; i++) {
    buffer_offset[i] = 0;
  }
  nbuffer_in2 = 0;
  do {
    nbuffer_in2 += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in2, line, parameters->ascii_in);
    if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0){
      if ((data_memcpy_reduce.type == ememcpy) || (data_memcpy_reduce.type == ereduce) ||
          (data_memcpy_reduce.type == ememcp_) || (data_memcpy_reduce.type == ereduc_) ||
          (data_memcpy_reduce.type == esmemcpy) || (data_memcpy_reduce.type == esmemcp_) ||
          (data_memcpy_reduce.type == esreduce) || (data_memcpy_reduce.type == esreduc_)) {
        if (data_memcpy_reduce.is_offset1 && data_memcpy_reduce.offset_number1 >= 0 && ((long int)data_memcpy_reduce.offset1 + (long int)data_memcpy_reduce.size > buffer_offset[data_memcpy_reduce.offset_number1])) {
          buffer_offset[data_memcpy_reduce.offset_number1] = (long int)data_memcpy_reduce.offset1 + (long int)data_memcpy_reduce.size;
        } else if (!data_memcpy_reduce.is_offset1 && ((long int)data_memcpy_reduce.offset1 + (long int)data_memcpy_reduce.size > shmem_max)) {
          shmem_max = (long int)data_memcpy_reduce.offset1 + (long int)data_memcpy_reduce.size;
        }
        if (data_memcpy_reduce.is_offset2 && data_memcpy_reduce.offset_number2 >= 0 && ((long int)data_memcpy_reduce.offset2 + (long int)data_memcpy_reduce.size > buffer_offset[data_memcpy_reduce.offset_number2])) {
          buffer_offset[data_memcpy_reduce.offset_number2] = (long int)data_memcpy_reduce.offset2 + (long int)data_memcpy_reduce.size;
        } else if (!data_memcpy_reduce.is_offset2 && ((long int)data_memcpy_reduce.offset2 + (long int)data_memcpy_reduce.size > shmem_max)) {
          shmem_max = (long int)data_memcpy_reduce.offset2 + (long int)data_memcpy_reduce.size;
        }
      }
      if ((data_memcpy_reduce.type == ewaitall) || (data_memcpy_reduce.type == ewaitany)) {
        ext_mpi_read_assembler_line(line, 0, "sd", &estring1, &o1, 0);
        if (o1 > locmem_max) {
          locmem_max = o1;
        }
      }
    }
  } while (flag > 0);
  buffer_offset2[0] = 0;
  for (i = 1; i < buffer_offset_max; i++) {
    if (buffer_offset[i - 1] >= CACHE_LINE_SIZE) {
      buffer_offset2[i] = buffer_offset2[i - 1] + buffer_offset[i - 1];
    } else {
      buffer_offset2[i] = buffer_offset2[i - 1] + CACHE_LINE_SIZE;
    }
  }
  if (shmem_max > buffer_offset2[buffer_offset_max - 1]) {
    buffer_offset2[buffer_offset_max - 1] = shmem_max;
  }
  buffer_offset2[buffer_offset_max - 1] = ((buffer_offset2[buffer_offset_max - 1] / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
  nbuffer_in2 = 0;
  parameters->locmem_max = locmem_max;
  parameters->shmem_buffer_offset_max = buffer_offset_max;
  free(parameters->shmem_buffer_offset);
  parameters->shmem_buffer_offset = (int*)malloc(sizeof(int) * parameters->shmem_buffer_offset_max);
  if (!parameters->shmem_buffer_offset)
    goto error;
  for (i = 0; i < parameters->shmem_buffer_offset_max; i++) {
    parameters->shmem_buffer_offset[i] = buffer_offset2[i];
    if (parameters->shmem_buffer_offset[i] != buffer_offset2[i]) {
      goto error;
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
  free(buffer_offset2);
  free(buffer_offset);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  free(buffer_offset2);
  free(buffer_offset);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
