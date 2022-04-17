#include "raw_code.h"
#include "constants.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mmsizes(int *msizes, int num_nodes, int msizes_max, int data0,
                   int data1) {
  if (num_nodes == msizes_max) {
    return (msizes[(data0) % num_nodes]);
  } else {
    if (num_nodes * num_nodes == msizes_max) {
      return (msizes[data0 * num_nodes + data1]);
    } else {
      if (1 == msizes_max) {
        return (msizes[0]);
      } else {
        printf("error in input data (mmsizes)\n");
        exit(1);
      }
    }
  }
}

int ext_mpi_generate_raw_code(char *buffer_in, char *buffer_out) {
  struct line_irecv_isend data_irecv_isend;
  struct line_memcpy_reduce data_memcpy_reduce;
  int node, num_nodes, size, add, add2, flag, node_rank,
      node_row_size = 1, node_column_size = 1, node_size, buffer_counter,
      size_s, adds;
  int num_comm = 0, nbuffer_out = 0, nbuffer_in = 0, *msizes, msizes_max, i, j,
      k = -1, m, n, o, q, size_level0 = 0, *size_level1 = NULL;
#ifdef NCCL_ENABLED
  int start = 0;
#endif
  char cline[1000];
  struct data_line **data = NULL;
  struct parameters_block *parameters;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
//  parameters->node /= (parameters->num_nodes / parameters->message_sizes_max);
//  parameters->num_nodes = parameters->message_sizes_max;
  node = parameters->node;
  num_nodes = parameters->num_nodes;
  node_row_size = parameters->node_row_size;
  node_column_size = parameters->node_column_size;
  node_rank = parameters->node_rank;
  msizes_max = parameters->message_sizes_max;
  msizes = parameters->message_sizes;
  node_size = node_row_size * node_column_size;
  nbuffer_in += i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &size_level0,
                                           &size_level1, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm raw_code\n");
    exit(2);
  }
  nbuffer_out +=
      ext_mpi_write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                              parameters->ascii_out);
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, cline, parameters->ascii_in);
    if (flag > 0) {
      nbuffer_out +=
          ext_mpi_write_line(buffer_out + nbuffer_out, cline, parameters->ascii_out);
    }
  } while (flag);
  num_nodes *= node_size;
  node = node * node_size + node_rank;
  for (m = 1; m < size_level0; m++) {
    if (m > 1) {
      nbuffer_out += ext_mpi_write_assembler_line(
          buffer_out + nbuffer_out, parameters->ascii_out, "s", enode_barrier);
    }
    buffer_counter = 1;
    for (o = 0; o < num_nodes; o++) {
      q = (num_nodes + node - o) % num_nodes;
      size = size_s = 0;
      for (n = 0; n < size_level1[m]; n++) {
        flag = 1;
        for (i = 0; (i < data[m][n].from_max) && flag; i++) {
          if ((data[m][n].from_node[i] == q) && (q != node)) {
            size += mmsizes(msizes, num_nodes / node_size, msizes_max,
                            data[m][n].frac, data[m][n].source);
            flag = 0;
          }
          if ((-data[m][n].from_node[i] - 10 == q) && (q != node)) {
            size_s += mmsizes(msizes, num_nodes / node_size, msizes_max,
                              data[m][n].frac, data[m][n].source);
            flag = 0;
          }
        }
      }
      if (size) {
#ifdef NCCL_ENABLED
  if (!start) {nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", estart); start = 1;}
#endif
        data_irecv_isend.type = eirecv;
        data_irecv_isend.buffer_type = eshmemo;
        data_irecv_isend.buffer_number = 0;
        data_irecv_isend.is_offset = 1;
        data_irecv_isend.offset_number = buffer_counter;
        data_irecv_isend.offset = 0;
        data_irecv_isend.size = size;
        data_irecv_isend.partner = q;
        data_irecv_isend.tag = num_comm;
        nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
        num_comm++;
        buffer_counter++;
      }
      if (size_s) {
        data_irecv_isend.type = eirec_;
        data_irecv_isend.buffer_type = eshmemo;
        data_irecv_isend.buffer_number = 0;
        data_irecv_isend.is_offset = 1;
        data_irecv_isend.offset_number = buffer_counter;
        data_irecv_isend.offset = 0;
        data_irecv_isend.size = size;
        data_irecv_isend.partner = q;
        data_irecv_isend.tag = num_comm;
        nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
        buffer_counter++;
      }
    }
    for (o = 0; o < num_nodes; o++) {
      q = (node + o) % num_nodes;
      add2 = add = adds = 0;
      for (n = 0; n < size_level1[m - 1]; n++) {
        for (j = 0; j < data[m - 1][n].to_max; j++) {
          if ((data[m - 1][n].to[j] == q) && (q != node)) {
            size = mmsizes(msizes, num_nodes / node_size, msizes_max,
                           data[m - 1][n].frac, data[m - 1][n].source);
            if (size) {
              data_memcpy_reduce.buffer_type1 = eshmemo;
              data_memcpy_reduce.buffer_number1 = 0;
              data_memcpy_reduce.is_offset1 = 1;
              data_memcpy_reduce.offset_number1 = buffer_counter;
              data_memcpy_reduce.offset1= add;
              data_memcpy_reduce.buffer_type2 = eshmemo;
              data_memcpy_reduce.buffer_number2 = 0;
              data_memcpy_reduce.is_offset2 = 1;
              data_memcpy_reduce.offset_number2 = 0;
              data_memcpy_reduce.offset2 = add2;
              data_memcpy_reduce.size = size;
              if (node_rank == 0) {
                data_memcpy_reduce.type = ememcpy;
              } else {
                data_memcpy_reduce.type = ememcp_;
              }
              nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
            }
            add += size;
          }
          if ((-data[m - 1][n].to[j] - 10 == q) && (q != node)) {
            size = mmsizes(msizes, num_nodes / node_size, msizes_max,
                           data[m - 1][n].frac, data[m - 1][n].source);
            if (size) {
              data_memcpy_reduce.buffer_type1 = eshmemo;
              data_memcpy_reduce.buffer_number1 = 0;
              data_memcpy_reduce.is_offset1 = 1;
              data_memcpy_reduce.offset_number1 = buffer_counter;
              data_memcpy_reduce.offset1= adds;
              data_memcpy_reduce.buffer_type2 = eshmemo;
              data_memcpy_reduce.buffer_number2 = 0;
              data_memcpy_reduce.is_offset2 = 1;
              data_memcpy_reduce.offset_number2 = 0;
              data_memcpy_reduce.offset2 = add2;
              data_memcpy_reduce.size = size;
              if (node_rank == 0) {
                data_memcpy_reduce.type = ememcpy;
              } else {
                data_memcpy_reduce.type = ememcp_;
              }
              nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
            }
            adds += size;
          }
        }
        add2 += mmsizes(msizes, num_nodes / node_size, msizes_max,
                        data[m - 1][n].frac, data[m - 1][n].source);
      }
      if (add) {
        nbuffer_out += ext_mpi_write_assembler_line(
            buffer_out + nbuffer_out, parameters->ascii_out, "s", enode_barrier);
        data_irecv_isend.type = eisend;
        data_irecv_isend.buffer_type = eshmemo;
        data_irecv_isend.buffer_number = 0;
        data_irecv_isend.is_offset = 1;
        data_irecv_isend.offset_number = buffer_counter;
        data_irecv_isend.offset = 0;
        data_irecv_isend.size = add;
        data_irecv_isend.partner = q;
        data_irecv_isend.tag = num_comm;
        nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
        num_comm++;
        buffer_counter++;
      }
      if (adds) {
        nbuffer_out += ext_mpi_write_assembler_line(
            buffer_out + nbuffer_out, parameters->ascii_out, "s", enode_barrier);
        data_irecv_isend.type = eisen_;
        data_irecv_isend.buffer_type = eshmemo;
        data_irecv_isend.buffer_number = 0;
        data_irecv_isend.is_offset = 1;
        data_irecv_isend.offset_number = buffer_counter;
        data_irecv_isend.offset = 0;
        data_irecv_isend.size = adds;
        data_irecv_isend.partner = q;
        data_irecv_isend.tag = num_comm;
        nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
        buffer_counter++;
      }
    }
    //    if (num_comm){
    nbuffer_out +=
        ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "sdsd", ewaitall, num_comm, elocmemp, 0);
#ifdef NCCL_ENABLED
        start = 0;
#endif
    //    }
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", enode_barrier);
    num_comm = 0;
    buffer_counter = 1;
    for (o = 0; o < num_nodes; o++) {
      q = (num_nodes + node - o) % num_nodes;
      size = 0;
      flag = 1;
      j = 0;
      for (n = 0; n < size_level1[m]; n++) {
        for (i = 0; i < data[m][n].from_max; i++) {
          if (((data[m][n].from_node[i] == q) && (q != node)) ||
              ((-data[m][n].from_node[i] - 10 == q) && (q != node))) {
            size += mmsizes(msizes, num_nodes / node_size, msizes_max,
                            data[m][n].frac, data[m][n].source);
            i = data[m][n].from_max;
            flag = 0;
            k = n;
          }
        }
        if (flag) {
          j += mmsizes(msizes, num_nodes / node_size, msizes_max,
                       data[m][n].frac, data[m][n].source);
        }
      }
      if (size && (q != node)) {
        data_memcpy_reduce.buffer_type1 = eshmemo;
        data_memcpy_reduce.buffer_number1 = 0;
        data_memcpy_reduce.is_offset1 = 1;
        data_memcpy_reduce.offset_number1 = 0;
        data_memcpy_reduce.offset1= j;
        data_memcpy_reduce.buffer_type2 = eshmemo;
        data_memcpy_reduce.buffer_number2 = 0;
        data_memcpy_reduce.is_offset2 = 1;
        data_memcpy_reduce.offset_number2 = buffer_counter;
        data_memcpy_reduce.offset2 = 0;
        data_memcpy_reduce.size = size;
        if ((data[m][k].from_node[0] != node) &&
            (-data[m][k].from_node[0] - 10 != node)) {
          if (node_rank == 0) {
            data_memcpy_reduce.type = ememcpy;
          } else {
            data_memcpy_reduce.type = ememcp_;
          }
        } else {
          if (node_rank == 0) {
            data_memcpy_reduce.type = ereduce;
          } else {
            data_memcpy_reduce.type = ereduc_;
          }
        }
        nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
        buffer_counter++;
      }
    }
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", enode_barrier);
    add = 0;
    for (n = 0; n < size_level1[m]; n++) {
      for (i = 0; i < data[m][n].from_max; i++) {
        if ((data[m][n].from_node[i] == node) &&
            (data[m][n].from_line[i] >= 0) && (data[m][n].from_line[i] != n)) {
          size = mmsizes(msizes, num_nodes / node_size, msizes_max,
                         data[m][n].frac, data[m][n].source);
          add2 = 0;
          for (k = 0; k < data[m][n].from_line[i]; k++) {
            add2 += mmsizes(msizes, num_nodes / node_size, msizes_max,
                            data[m][k].frac, data[m][k].source);
          }
          data_memcpy_reduce.buffer_type1 = eshmemo;
          data_memcpy_reduce.buffer_number1 = 0;
          data_memcpy_reduce.is_offset1 = 1;
          data_memcpy_reduce.offset_number1 = 0;
          data_memcpy_reduce.offset1= add;
          data_memcpy_reduce.buffer_type2 = eshmemo;
          data_memcpy_reduce.buffer_number2 = 0;
          data_memcpy_reduce.is_offset2 = 1;
          data_memcpy_reduce.offset_number2 = 0;
          data_memcpy_reduce.offset2 = add2;
          data_memcpy_reduce.size = size;
          if (node_rank == 0) {
            data_memcpy_reduce.type = ereduce;
          } else {
            data_memcpy_reduce.type = ereduc_;
          }
          nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
        }
      }
      add += mmsizes(msizes, num_nodes / node_size, msizes_max, data[m][n].frac,
                     data[m][n].source);
    }
    add = 0;
    for (n = 0; n < size_level1[m]; n++) {
      for (i = 0; i < data[m][n].from_max; i++) {
        if ((data[m][n].from_node[i] == node) &&
            (data[m][n].from_line[i] <= -10) &&
            (-10 - data[m][n].from_line[i] < n)) {
          size = mmsizes(msizes, num_nodes / node_size, msizes_max,
                         data[m][n].frac, data[m][n].source);
          add2 = 0;
          for (k = 0; k < -10 - data[m][n].from_line[i]; k++) {
            add2 += mmsizes(msizes, num_nodes / node_size, msizes_max,
                            data[m][k].frac, data[m][k].source);
          }
          data_memcpy_reduce.buffer_type1 = eshmemo;
          data_memcpy_reduce.buffer_number1 = 0;
          data_memcpy_reduce.is_offset1 = 1;
          data_memcpy_reduce.offset_number1 = 0;
          data_memcpy_reduce.offset1= add;
          data_memcpy_reduce.buffer_type2 = eshmemo;
          data_memcpy_reduce.buffer_number2 = 0;
          data_memcpy_reduce.is_offset2 = 1;
          data_memcpy_reduce.offset_number2 = 0;
          data_memcpy_reduce.offset2 = add2;
          data_memcpy_reduce.size = size;
          if (node_rank == 0) {
            data_memcpy_reduce.type = ememcpy;
          } else {
            data_memcpy_reduce.type = ememcp_;
          }
          nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
        }
      }
      add += mmsizes(msizes, num_nodes / node_size, msizes_max, data[m][n].frac,
                     data[m][n].source);
    }
  }
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(size_level0, size_level1, data);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(size_level0, size_level1, data);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
