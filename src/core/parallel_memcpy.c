#include "parallel_memcpy.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_parallel_memcpy(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce, data_memcpy_reduce_, data_memcpy_reduce_old;
  int add, flag, flag3, node_rank, node_row_size = 1, node_column_size = 1, node_size;
  int nbuffer_out = 0, nbuffer_in = 0, i, reset = 1, type_size = 1, p1 = -1;
  char line[1000], line2[1000];
  enum eassembler_type estring1;
  struct parameters_block *parameters;
  data_memcpy_reduce.offset1 = data_memcpy_reduce.offset2 = data_memcpy_reduce_.offset1 = data_memcpy_reduce_.offset2 =
  data_memcpy_reduce_old.offset1 = data_memcpy_reduce_old.offset2 =
  data_memcpy_reduce.size = data_memcpy_reduce_.size = data_memcpy_reduce_old.size = -1;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  node_row_size = parameters->socket_row_size;
  node_column_size = parameters->socket_column_size;
  node_rank = parameters->socket_rank;
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
  node_size = node_row_size * node_column_size;
  do {
    flag3 = ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce);
    data_memcpy_reduce_ = data_memcpy_reduce;
    if ((flag3 > 0) && (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0)) {
      reset = 0;
      flag = 0;
      if (estring1 == esocket_barrier) {
        reset = 1;
        flag = 1;
      }
      if (data_memcpy_reduce_old.offset1 != data_memcpy_reduce.offset1) {
        data_memcpy_reduce_old.offset1 = data_memcpy_reduce.offset1;
        reset = 1;
      }
      if (reset) {
        p1 = 0;
        if (flag) {
          p1 += ext_mpi_read_line(buffer_in + nbuffer_in + p1, line2,
                                  parameters->ascii_in);
        }
        flag = 1;
        data_memcpy_reduce_old.size = 0;
        while (flag) {
          p1 += flag = ext_mpi_read_line(buffer_in + nbuffer_in + p1, line2,
                                         parameters->ascii_in);
          ext_mpi_read_memcpy_reduce(line2, &data_memcpy_reduce);
          if ((flag > 0) && (ext_mpi_read_assembler_line(line2, 0, "s", &estring1) >= 0)) {
            if (estring1 == esocket_barrier) {
              flag = 0;
            }
            if (data_memcpy_reduce_old.offset1 != data_memcpy_reduce.offset1) {
              flag = 0;
            }
            if (flag) {
              if ((data_memcpy_reduce.type == ememcpy) || (data_memcpy_reduce.type == ememcp_) ||
                  (data_memcpy_reduce.type == ereduce) || (data_memcpy_reduce.type == ereduc_)) {
                if ((data_memcpy_reduce.buffer_type1 == eshmemo) && (data_memcpy_reduce.buffer_type2 == eshmemo)) {
                  if (data_memcpy_reduce.size > data_memcpy_reduce_old.size) {
                    data_memcpy_reduce_old.size = data_memcpy_reduce.size;
                  }
                }
              }
            }
          } else {
            flag = 0;
          }
        }
        data_memcpy_reduce = data_memcpy_reduce_;
      }
      flag = 1;
      if ((data_memcpy_reduce.type == ememcpy) || (data_memcpy_reduce.type == ememcp_) ||
          (data_memcpy_reduce.type == ereduce) || (data_memcpy_reduce.type == ereduc_)) {
        if ((data_memcpy_reduce.buffer_type1 == eshmemo) && (data_memcpy_reduce.buffer_type2 == eshmemo)) {
          if (node_rank < node_size) {
            add = ((data_memcpy_reduce_old.size / type_size) / node_size) * node_rank;
            i = (data_memcpy_reduce_old.size / type_size) / node_size;
            if (node_rank < (data_memcpy_reduce_old.size / type_size) % node_size) {
              add += node_rank;
              i++;
            } else {
              add += (data_memcpy_reduce_old.size / type_size) % node_size;
            }
            if (add * type_size > data_memcpy_reduce.size) {
              i = 0;
            } else {
              if ((add + i) * type_size > data_memcpy_reduce.size) {
                i = data_memcpy_reduce.size / type_size - add;
              }
            }
          } else {
            i = add = 0;
          }
          data_memcpy_reduce.size = i * type_size;
          data_memcpy_reduce.offset1 += add * type_size;
          data_memcpy_reduce.offset2 += add * type_size;
          if (data_memcpy_reduce.type == ememcp_) {
            data_memcpy_reduce.type = ememcpy;
          }
          if (data_memcpy_reduce.type == ereduc_) {
            data_memcpy_reduce.type = ereduce;
          }
          if (data_memcpy_reduce.size) {
            nbuffer_out += ext_mpi_write_memcpy_reduce(
                buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
          }
          flag = 0;
        }
      }
      if (flag3 && flag) {
        nbuffer_out +=
            ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
      }
    }
    buffer_in += flag3;
  } while (flag3);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
