#include "buffer_offset.h"
#include "constants.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_buffer_offset(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, nbuffer_in2 = 0, bo1, bo2, o1, o2, size,
      *buffer_offset = NULL, *buffer_offset2 = NULL, buffer_offset_max = 0,
      locmem_max = 0, flag, i, shmem_max = 0;
  char line[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1, estring2, estring3, estring4, estring5;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  buffer_in += nbuffer_in;
  do {
    nbuffer_in2 += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in2, line, parameters->ascii_in);
    if (ext_mpi_read_assembler_line_s(line, &estring1, 0) >= 0) {
      if ((estring1 == ememcpy) || (estring1 == ereduce) ||
          (estring1 == ememcp_) || (estring1 == ereduc_) ||
          (estring1 == esmemcpy) || (estring1 == esmemcp_) ||
          (estring1 == esreduce) || (estring1 == esreduc_)) {
        if (ext_mpi_read_assembler_line_ssdsdsdsdd(line, &estring1, &estring2, &bo1,
                                                   &estring3, &o1, &estring4, &bo2,
                                                   &estring5, &o2, &size, 0) >= 0) {
          if (bo1 > buffer_offset_max) {
            buffer_offset_max = bo1;
          }
          if (bo2 > buffer_offset_max) {
            buffer_offset_max = bo2;
          }
        }
      }
    }
  } while (flag > 0);
  buffer_offset_max += 2;
  buffer_offset = (int *)malloc(sizeof(int) * buffer_offset_max);
  if (!buffer_offset)
    goto error;
  buffer_offset2 = (int *)malloc(sizeof(int) * buffer_offset_max);
  if (!buffer_offset2)
    goto error;
  for (i = 0; i < buffer_offset_max; i++) {
    buffer_offset[i] = 0;
  }
  nbuffer_in2 = 0;
  do {
    nbuffer_in2 += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in2, line, parameters->ascii_in);
    if (ext_mpi_read_assembler_line_s(line, &estring1, 0) >= 0) {
      if ((estring1 == ememcpy) || (estring1 == ereduce) ||
          (estring1 == ememcp_) || (estring1 == ereduc_) ||
          (estring1 == esmemcpy) || (estring1 == esmemcp_) ||
          (estring1 == esreduce) || (estring1 == esreduc_)) {
        if (ext_mpi_read_assembler_line_ssdsdsdsdd(line, &estring1, &estring2, &bo1,
                                                   &estring3, &o1, &estring4, &bo2,
                                                   &estring5, &o2, &size, 0) >= 0) {
          if (bo1 >= 0) {
            if (o1 + size > buffer_offset[bo1]) {
              buffer_offset[bo1] = o1 + size;
            }
	  }
          if (bo2 >= 0) {
            if (o2 + size > buffer_offset[bo2]) {
              buffer_offset[bo2] = o2 + size;
            }
          }
        } else if (ext_mpi_read_assembler_line_ssdsdsdd(line, &estring1, &estring2, &bo1,
                                                        &estring3, &o1, &estring4,
                                                        &o2, &size, 0) >= 0) {
          if (bo1 >= 0) {
            if (o1 + size > buffer_offset[bo1]) {
              buffer_offset[bo1] = o1 + size;
            }
          }
          if (o2 + size > shmem_max) {
            shmem_max = o2 + size;
          }
        } else if (ext_mpi_read_assembler_line_ssdsdd(line, &estring1, &estring2, &o1,
                                                      &estring3, &o2, &size, 0) >= 0) {
          if (o1 + size > shmem_max) {
            shmem_max = o1 + size;
          }
          if (o2 + size > shmem_max) {
            shmem_max = o2 + size;
          }
        }
      }
      if ((estring1 == ewaitall)||(estring1 == ewaitany)) {
        ext_mpi_read_assembler_line_sd(line, &estring1, &o1, 0);
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
  parameters->shmem_buffer_offset = buffer_offset2;
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
  free(buffer_offset);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  free(buffer_offset);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
