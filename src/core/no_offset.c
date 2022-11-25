#include "no_offset.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_no_offset(char *buffer_in, char *buffer_out) {
  struct line_irecv_isend data_irecv_isend;
  struct line_memcpy_reduce data_memcpy_reduce;
  int nbuffer_out = 0, nbuffer_in = 0, *buffer_offset, buffer_offset_max, flag;
  char line[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1;
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  buffer_offset_max = parameters->shmem_buffer_offset_max;
  buffer_offset = parameters->shmem_buffer_offset;
  parameters->shmem_max = buffer_offset[buffer_offset_max - 1];
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
data_memcpy_reduce.type = enop;
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag > 0) {
      if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
        if (((estring1 == eirecv) || (estring1 == eisend) ||
             (estring1 == eirec_) || (estring1 == eisen_)) &&
            (ext_mpi_read_irecv_isend(line, &data_irecv_isend) >= 0)) {
          if (data_irecv_isend.is_offset) {
            data_irecv_isend.is_offset = 0;
            if (data_irecv_isend.offset_number >= 0) {
              data_irecv_isend.offset += buffer_offset[data_irecv_isend.offset_number];
            } else {
              data_irecv_isend.offset += parameters->shmem_max;
            }
          }
          nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
        } else if (((estring1 == ememcpy) || (estring1 == ereduce) ||
                   (estring1 == ememcp_) || (estring1 == ereduc_) ||
                   (estring1 == esmemcpy) || (estring1 == esmemcp_) ||
                   (estring1 == esreduce)) &&
                   (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0)) {
          if (data_memcpy_reduce.is_offset1) {
            data_memcpy_reduce.is_offset1 = 0;
            if (data_memcpy_reduce.offset_number1 >= 0) {
              data_memcpy_reduce.offset1 += buffer_offset[data_memcpy_reduce.offset_number1];
            } else {
              data_memcpy_reduce.offset1 += parameters->shmem_max;
            }
          }
          if (data_memcpy_reduce.is_offset2) {
            data_memcpy_reduce.is_offset2 = 0;
            if (data_memcpy_reduce.offset_number2 >= 0) {
              data_memcpy_reduce.offset2 += buffer_offset[data_memcpy_reduce.offset_number2];
            } else {
              data_memcpy_reduce.offset2 += parameters->shmem_max;
            }
          }
          nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
        } else {
          nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
        }
      }
    }
  } while (flag > 0);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
}
