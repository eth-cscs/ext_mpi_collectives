#include "raw_code.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mmsizes(int *msizes, int num_nodes, int msizes_max, int data0) {
  if (num_nodes == msizes_max) {
    return (msizes[(data0) % num_nodes]);
  } else if (1 == msizes_max) {
    return (msizes[0]);
  } else {
    printf("error in input data (mmsizes)\n");
    exit(1);
  }
}

int ext_mpi_generate_raw_code(char *buffer_in, char *buffer_out) {
  struct line_irecv_isend data_irecv_isend;
  struct line_memcpy_reduce data_memcpy_reduce;
  int node, num_nodes, size, add, add2, flag, node_rank,
      node_row_size = 1, node_column_size = 1, node_size, buffer_counter,
      size_s, adds;
  int num_comm = 0, num_comms = 0, nbuffer_out = 0, nbuffer_in = 0, *msizes, msizes_max, i, j,
      k = -1, m, n, o, q;
#ifdef NCCL_ENABLED
  int start = 0;
#endif
  char cline[1000];
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
  node = parameters->socket;
  num_nodes = parameters->num_sockets;
  node_row_size = parameters->socket_row_size;
  node_column_size = parameters->socket_column_size;
  node_rank = parameters->socket_rank;
  msizes_max = parameters->message_sizes_max;
  msizes = parameters->message_sizes;
  node_size = node_row_size * node_column_size;
  nbuffer_in += i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm raw_code\n");
    exit(2);
  }
  nbuffer_out +=
      ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
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
  for (m = 1; m < data.num_blocks; m++) {
    if (m > 1) {
      nbuffer_out += ext_mpi_write_assembler_line(
          buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
    }
    buffer_counter = 1;
    for (o = 0; o < num_nodes; o++) {
      q = (num_nodes + node - o) % num_nodes;
      size = size_s = 0;
      for (n = 0; n < data.blocks[m].num_lines; n++) {
        flag = 1;
        for (i = 0; (i < data.blocks[m].lines[n].recvfrom_max) && flag; i++) {
          if ((data.blocks[m].lines[n].recvfrom_node[i] == q) && (q != node)) {
            size += mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
            flag = 0;
          }
          if ((-data.blocks[m].lines[n].recvfrom_node[i] - 10 == q) && (q != node)) {
            size_s += mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
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
        data_irecv_isend.size = size_s;
        data_irecv_isend.partner = q;
        data_irecv_isend.tag = num_comms;
        nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
        num_comms++;
        buffer_counter++;
      }
    }
    for (o = 0; o < num_nodes; o++) {
      q = (node + o) % num_nodes;
      add2 = add = adds = 0;
      for (n = 0; n < data.blocks[m].num_lines; n++) {
        for (j = 0; j < data.blocks[m].lines[n].sendto_max; j++) {
          if ((data.blocks[m].lines[n].sendto_node[j] == q) && (q != node)) {
            size = mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
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
          if ((-data.blocks[m].lines[n].sendto_node[j] - 10 == q) && (q != node)) {
            size = mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
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
        add2 += mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
      }
      if (add) {
        nbuffer_out += ext_mpi_write_assembler_line(
            buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
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
            buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
        data_irecv_isend.type = eisen_;
        data_irecv_isend.buffer_type = eshmemo;
        data_irecv_isend.buffer_number = 0;
        data_irecv_isend.is_offset = 1;
        data_irecv_isend.offset_number = buffer_counter;
        data_irecv_isend.offset = 0;
        data_irecv_isend.size = adds;
        data_irecv_isend.partner = q;
        data_irecv_isend.tag = num_comms;
        nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
        num_comms++;
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
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
    num_comm = num_comms = 0;
    buffer_counter = 1;
    for (o = 0; o < num_nodes; o++) {
      q = (num_nodes + node - o) % num_nodes;
      size = 0;
      flag = 1;
      j = 0;
      for (n = 0; n < data.blocks[m].num_lines; n++) {
        for (i = 0; i < data.blocks[m].lines[n].recvfrom_max; i++) {
          if (((data.blocks[m].lines[n].recvfrom_node[i] == q) && (q != node)) ||
              ((-data.blocks[m].lines[n].recvfrom_node[i] - 10 == q) && (q != node))) {
            size += mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
            i = data.blocks[m].lines[n].recvfrom_max;
            flag = 0;
            k = n;
          }
        }
        if (flag) {
          j += mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
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
        if ((data.blocks[m].lines[k].recvfrom_node[0] != node) &&
            (-data.blocks[m].lines[k].recvfrom_node[0] - 10 != node)) {
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
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
    for (n = add = 0; n < data.blocks[m].num_lines; n++) {
      for (i = 0; i < data.blocks[m].lines[n].reducefrom_max; i++) {
        size = mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
        add2 = 0;
        for (k = 0; k < data.blocks[m].lines[n].reducefrom[i]; k++) {
          add2 += mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[k].frac);
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
        if (node_rank == 0 && size) {
          data_memcpy_reduce.type = ereduce;
        } else {
          data_memcpy_reduce.type = ereduc_;
        }
        nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
      }
      add += mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
    }
    for (n = add = 0; n < data.blocks[m].num_lines; n++) {
      for (i = 0; i < data.blocks[m].lines[n].copyreducefrom_max; i++) {
        size = mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
        add2 = 0;
        for (k = 0; k < data.blocks[m].lines[n].copyreducefrom[i]; k++) {
          add2 += mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[k].frac);
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
	if (i == 0) {
          if (node_rank == 0 && size) {
            data_memcpy_reduce.type = ememcpy;
          } else {
            data_memcpy_reduce.type = ememcp_;
          }
	} else {
          if (node_rank == 0 && size) {
            data_memcpy_reduce.type = ereduce;
          } else {
            data_memcpy_reduce.type = ereduc_;
          }
        }
        nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
      }
      add += mmsizes(msizes, num_nodes / node_size, msizes_max, data.blocks[m].lines[n].frac);
    }
  }
  nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
