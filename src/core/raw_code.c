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
  int node, num_nodes, size, add, add2, flag, node_rank,
      node_row_size = 1, node_column_size = 1, node_size, buffer_counter,
      size_s, adds;
  int num_comm = 0, nbuffer_out = 0, nbuffer_in = 0, *msizes, msizes_max, i, j,
      k = -1, m, n, o, q, size_level0 = 0, *size_level1 = NULL;
  char cline[1000];
  struct data_line **data = NULL;
  struct parameters_block *parameters;
  nbuffer_in += i = read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  node = parameters->node;
  num_nodes = parameters->num_nodes;
  node_row_size = parameters->node_row_size;
  node_column_size = parameters->node_column_size;
  node_rank = parameters->node_rank;
  msizes_max = parameters->message_sizes_max;
  msizes = parameters->message_sizes;
  node_size = node_row_size * node_column_size;
  nbuffer_in += i = read_algorithm(buffer_in + nbuffer_in, &size_level0,
                                   &size_level1, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm raw_code\n");
    exit(2);
  }
  nbuffer_out +=
      write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                      parameters->ascii_out);
  do {
    nbuffer_in += flag =
        read_line(buffer_in + nbuffer_in, cline, parameters->ascii_in);
    if (flag > 0) {
      nbuffer_out +=
          write_line(buffer_out + nbuffer_out, cline, parameters->ascii_out);
    }
  } while (flag);
  num_nodes *= node_size;
  node = node * node_size + node_rank;
  for (m = 1; m < size_level0; m++) {
    if (m > 1) {
      nbuffer_out += write_assembler_line_s(
          buffer_out + nbuffer_out, enode_barrier, parameters->ascii_out);
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
        nbuffer_out += write_assembler_line_ssdsdddd(
            buffer_out + nbuffer_out, eirecv, eshmempbuffer_offseto,
            buffer_counter, eshmempbuffer_offsetcp, 0, size, q, num_comm,
            parameters->ascii_out);
        num_comm++;
        buffer_counter++;
      }
      if (size_s) {
        nbuffer_out += write_assembler_line_ssdsdddd(
            buffer_out + nbuffer_out, eirec_, eshmempbuffer_offseto,
            buffer_counter, eshmempbuffer_offsetcp, 0, size_s, q, num_comm,
            parameters->ascii_out);
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
              if (node_rank == 0) {
                nbuffer_out += write_assembler_line_ssdsdsdsdd(
                    buffer_out + nbuffer_out, ememcpy, eshmempbuffer_offseto,
                    buffer_counter, eshmempbuffer_offsetcp, add,
                    eshmempbuffer_offseto, 0, eshmempbuffer_offsetcp, add2,
                    size, parameters->ascii_out);
              } else {
                nbuffer_out += write_assembler_line_ssdsdsdsdd(
                    buffer_out + nbuffer_out, ememcp_, eshmempbuffer_offseto,
                    buffer_counter, eshmempbuffer_offsetcp, add,
                    eshmempbuffer_offseto, 0, eshmempbuffer_offsetcp, add2,
                    size, parameters->ascii_out);
              }
            }
            add += size;
          }
          if ((-data[m - 1][n].to[j] - 10 == q) && (q != node)) {
            size = mmsizes(msizes, num_nodes / node_size, msizes_max,
                           data[m - 1][n].frac, data[m - 1][n].source);
            if (size) {
              if (node_rank == 0) {
                nbuffer_out += write_assembler_line_ssdsdsdsdd(
                    buffer_out + nbuffer_out, ememcpy, eshmempbuffer_offseto,
                    buffer_counter, eshmempbuffer_offsetcp, adds,
                    eshmempbuffer_offseto, 0, eshmempbuffer_offsetcp, add2,
                    size, parameters->ascii_out);
              } else {
                nbuffer_out += write_assembler_line_ssdsdsdsdd(
                    buffer_out + nbuffer_out, ememcp_, eshmempbuffer_offseto,
                    buffer_counter, eshmempbuffer_offsetcp, adds,
                    eshmempbuffer_offseto, 0, eshmempbuffer_offsetcp, add2,
                    size, parameters->ascii_out);
              }
            }
            adds += size;
          }
        }
        add2 += mmsizes(msizes, num_nodes / node_size, msizes_max,
                        data[m - 1][n].frac, data[m - 1][n].source);
      }
      if (add) {
        nbuffer_out += write_assembler_line_s(
            buffer_out + nbuffer_out, enode_barrier, parameters->ascii_out);
        nbuffer_out += write_assembler_line_ssdsdddd(
            buffer_out + nbuffer_out, eisend, eshmempbuffer_offseto,
            buffer_counter, eshmempbuffer_offsetcp, 0, add, q, num_comm,
            parameters->ascii_out);
        num_comm++;
        buffer_counter++;
      }
      if (adds) {
        nbuffer_out += write_assembler_line_s(
            buffer_out + nbuffer_out, enode_barrier, parameters->ascii_out);
        nbuffer_out += write_assembler_line_ssdsdddd(
            buffer_out + nbuffer_out, eisen_, eshmempbuffer_offseto,
            buffer_counter, eshmempbuffer_offsetcp, 0, adds, q, num_comm,
            parameters->ascii_out);
        buffer_counter++;
      }
    }
    //    if (num_comm){
    nbuffer_out +=
        write_assembler_line_sdsd(buffer_out + nbuffer_out, ewaitall, num_comm,
                                  elocmemp, 0, parameters->ascii_out);
    //    }
    nbuffer_out += write_assembler_line_s(buffer_out + nbuffer_out,
                                          enode_barrier, parameters->ascii_out);
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
        if ((data[m][k].from_node[0] != node) &&
            (-data[m][k].from_node[0] - 10 != node)) {
          if (node_rank == 0) {
            nbuffer_out += write_assembler_line_ssdsdsdsdd(
                buffer_out + nbuffer_out, ememcpy, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, j, eshmempbuffer_offseto,
                buffer_counter, eshmempbuffer_offsetcp, 0, size,
                parameters->ascii_out);
          } else {
            nbuffer_out += write_assembler_line_ssdsdsdsdd(
                buffer_out + nbuffer_out, ememcp_, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, j, eshmempbuffer_offseto,
                buffer_counter, eshmempbuffer_offsetcp, 0, size,
                parameters->ascii_out);
          }
        } else {
          if (node_rank == 0) {
            nbuffer_out += write_assembler_line_ssdsdsdsdd(
                buffer_out + nbuffer_out, ereduce, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, j, eshmempbuffer_offseto,
                buffer_counter, eshmempbuffer_offsetcp, 0, size,
                parameters->ascii_out);
          } else {
            nbuffer_out += write_assembler_line_ssdsdsdsdd(
                buffer_out + nbuffer_out, ereduc_, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, j, eshmempbuffer_offseto,
                buffer_counter, eshmempbuffer_offsetcp, 0, size,
                parameters->ascii_out);
          }
        }
        buffer_counter++;
      }
    }
    nbuffer_out += write_assembler_line_s(buffer_out + nbuffer_out,
                                          enode_barrier, parameters->ascii_out);
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
          if (node_rank == 0) {
            nbuffer_out += write_assembler_line_ssdsdsdsdd(
                buffer_out + nbuffer_out, ereduce, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, add, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, add2, size, parameters->ascii_out);
          } else {
            nbuffer_out += write_assembler_line_ssdsdsdsdd(
                buffer_out + nbuffer_out, ereduc_, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, add, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, add2, size, parameters->ascii_out);
          }
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
          if (node_rank == 0) {
            nbuffer_out += write_assembler_line_ssdsdsdsdd(
                buffer_out + nbuffer_out, ememcpy, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, add, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, add2, size, parameters->ascii_out);
          } else {
            nbuffer_out += write_assembler_line_ssdsdsdsdd(
                buffer_out + nbuffer_out, ememcp_, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, add, eshmempbuffer_offseto, 0,
                eshmempbuffer_offsetcp, add2, size, parameters->ascii_out);
          }
        }
      }
      add += mmsizes(msizes, num_nodes / node_size, msizes_max, data[m][n].frac,
                     data[m][n].source);
    }
  }
  nbuffer_out += write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  delete_algorithm(size_level0, size_level1, data);
  delete_parameters(parameters);
  return nbuffer_out;
error:
  delete_algorithm(size_level0, size_level1, data);
  delete_parameters(parameters);
  return ERROR_MALLOC;
}
