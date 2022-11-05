#include "reduce_copyin.h"
#include "constants.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RADIX 7

static int copyin(struct parameters_block *parameters, struct data_algorithm data, int type_size, int num_tasks, int my_task, int block_offset, char *buffer_out){
  struct line_memcpy_reduce data_memcpy_reduce;
  int nbuffer_out=0, remote_offsets[data.blocks[0].num_lines+1], total_message_size=0, my_message_size, my_message_offset, i, j, k, start_copy, stop_copy, add, add2, size;
  enum eassembler_type op_type;
  for (i = 0; i < data.blocks[0].num_lines; i++) {
    total_message_size+=parameters->message_sizes[i];
  }
  remote_offsets[0]=0;
  for (i=0; i<data.blocks[0].num_lines; i++){
    remote_offsets[i+1]=remote_offsets[i]+parameters->message_sizes[data.blocks[0].lines[i].frac];
  }
  for (i=0; i<num_tasks; i++){
    my_message_size = (total_message_size/type_size)/num_tasks;
    my_message_offset = my_message_size*((my_task+i)%num_tasks);
    if ((my_task+i)%num_tasks<(total_message_size/type_size)%num_tasks){
      my_message_size++;
      my_message_offset+=(my_task+i)%num_tasks;
    }else{
      my_message_offset+=(total_message_size/type_size)%num_tasks;
    }
    my_message_size*=type_size;
    my_message_offset*=type_size;
    for (j=0; j<data.blocks[0].num_lines; j++){
      for (k=0; data.blocks[0].lines[k].frac!=j; k++);
      if ((remote_offsets[j+1]>=my_message_offset)&&(remote_offsets[j]<my_message_offset+my_message_size)){
        if (remote_offsets[j]<my_message_offset){
          start_copy=my_message_offset;
        }else{
          start_copy=remote_offsets[j];
        }
        if (remote_offsets[j+1]<my_message_offset+my_message_size){
          stop_copy=remote_offsets[j+1];
        }else{
          stop_copy=my_message_offset+my_message_size;
        }
//nbuffer_out += sprintf(buffer_out + nbuffer_out, "aaa %d %d %d %d\n", my_message_offset, my_message_offset+my_message_size, remote_offsets[j], remote_offsets[j+1]);
        size=stop_copy-start_copy;
        if (size>0){
          add=start_copy+block_offset*total_message_size;
          add2=(start_copy-remote_offsets[k]+total_message_size)%total_message_size;
          if (i==0){
            op_type=ememcpy;
          }else{
            op_type=ereduce;
          }
          data_memcpy_reduce.type = op_type;
          data_memcpy_reduce.buffer_type1 = eshmemo;
          data_memcpy_reduce.buffer_number1 = 0;
          data_memcpy_reduce.is_offset1 = 0;
          data_memcpy_reduce.offset1 = add;
          data_memcpy_reduce.buffer_type2 = esendbufp;
          data_memcpy_reduce.is_offset2 = 0;
          data_memcpy_reduce.offset2 = add2;
          data_memcpy_reduce.size = size;
          nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
        }
      }
    }
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_cycl_barrier);
  }
  nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
  return nbuffer_out;
}

static int reduce(struct parameters_block *parameters, struct data_algorithm data, int type_size, int num_tasks, int my_task, int num_blocks, int *block_offsets, char *buffer_out){
  struct line_memcpy_reduce data_memcpy_reduce;
  int nbuffer_out=0, total_message_size=0, my_message_size, my_message_offset, i, j, add, add2, size;
  for (i = 0; i < data.blocks[0].num_lines; i++) {
    total_message_size+=parameters->message_sizes[i];
  }
  for (i=0; i<num_tasks; i++){
    my_message_size = (total_message_size/type_size)/num_tasks;
    my_message_offset = my_message_size*my_task;
    if (my_task<(total_message_size/type_size)%num_tasks){
      my_message_size++;
      my_message_offset+=my_task;
    }else{
      my_message_offset+=(total_message_size/type_size)%num_tasks;
    }
    my_message_size*=type_size;
    my_message_offset*=type_size;
  }
  size=my_message_size;
  if (size>0){
    add=my_message_offset+block_offsets[0]*total_message_size;
    for (j=1; j<num_blocks; j++){
      add2=my_message_offset+block_offsets[j]*total_message_size;
      data_memcpy_reduce.type = esreduce;
      data_memcpy_reduce.buffer_type1 = eshmemo;
      data_memcpy_reduce.buffer_number1 = 0;
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
  return nbuffer_out;
}

int ext_mpi_generate_reduce_copyin(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce;
  int num_nodes = 1, size, add, add2, node_rank, node_row_size = 1,
      node_column_size = 1, node_size, *counts = NULL, counts_max = 0,
      *displs = NULL, *iocounts = NULL, iocounts_max = 0, *iodispls = NULL,
      *lcounts = NULL, *ldispls = NULL, lrank_row, lrank_column;
  int nbuffer_out = 0, nbuffer_in = 0, *mcounts = NULL, *moffsets = NULL, i, j,
      k, m, copy_method = 0, total_message_size=0;
  int size_level0 = 0, *size_level1 = NULL, collective_type = 1;
  int barriers_size, step, substep, add_local, size_local, type_size = 1, tsize;
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
  num_nodes = parameters->num_sockets;
  node_rank = parameters->socket_rank;
  node_row_size = parameters->socket_row_size;
  node_column_size = parameters->socket_column_size;
  copy_method = parameters->copy_method;
  if (parameters->collective_type == collective_type_allgatherv) {
    collective_type = 0;
  }
  if (parameters->collective_type == collective_type_reduce_scatter) {
    collective_type = 2;
  }
  mcounts = parameters->message_sizes;
  counts_max = parameters->counts_max;
  counts = parameters->counts;
  iocounts_max = parameters->iocounts_max;
  iocounts = parameters->iocounts;
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
    printf("error reading algorithm reduce_copyin\n");
    exit(2);
  }
  nbuffer_out += ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
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
  lrank_row = node_rank / counts_max;
  lrank_column = node_rank % counts_max;
  lcounts = (int *)malloc(sizeof(int) * (node_size / counts_max));
  if (!lcounts)
    goto error;
  ldispls = (int *)malloc(sizeof(int) * (node_size / counts_max + 1));
  if (!ldispls)
    goto error;
  for (i = 0; i < node_size / counts_max; i++) {
    lcounts[i] = (counts[lrank_column] / type_size) / (node_size / counts_max);
    if (i < (counts[lrank_column] / type_size) % (node_size / counts_max)) {
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
  if ((size_level1[0] == 1) && (collective_type == 0)) {
    for (i = 0; i < num_nodes; i++) {
      moffsets[i] = moffsets[num_nodes];
    }
  }
  // nbuffer_out+=write_assembler_line_ssdsdsdsdd(buffer_out+nbuffer_out,
  // ememcpy, eshmempbuffer_offseto, buffer_counter, eshmempbuffer_offsetcp, add,
  // eshmempbuffer_offseto, 0, eshmempbuffer_offsetcp, add2, size, parameters->as
  nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
  if (collective_type) {
    if (((collective_type == 2) && (parameters->root >= 0)) ||
        (parameters->root <= -10)) {
      if ((parameters->socket * parameters->socket_row_size +
               parameters->socket_rank ==
           parameters->root) ||
          (parameters->socket * parameters->socket_row_size +
               parameters->socket_rank ==
           -10 - parameters->root)) {
        add = 0;
        for (i = 0; i < num_nodes; i++) {
          j = (num_nodes + i + parameters->socket) % num_nodes;
          data_memcpy_reduce.type = ememcpy;
          data_memcpy_reduce.buffer_type1 = eshmemo;
          data_memcpy_reduce.buffer_number1 = 0;
          data_memcpy_reduce.is_offset1 = 0;
          data_memcpy_reduce.offset1 = add;
          data_memcpy_reduce.buffer_type2 = esendbufp;
          data_memcpy_reduce.buffer_number2 = 0;
          data_memcpy_reduce.is_offset2 = 0;
          data_memcpy_reduce.offset2 = moffsets[j];
          data_memcpy_reduce.size = mcounts[j];
          nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
          add += mcounts[j];
        }
      }
      nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
    } else {
      if ((copy_method == 3) && (node_size == 1)) {
        copy_method = 0;
      }
      switch (copy_method) {
      case 0:
        if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
          add = CACHE_LINE_SIZE * lrank_row + ldispls[lrank_column];
        } else {
          add = moffsets[num_nodes] * lrank_row + ldispls[lrank_column];
        }
        for (i = 0; i < size_level1[0]; i++) {
          size = mcounts[data.blocks[0].lines[i].frac];
          add2 = moffsets[data.blocks[0].lines[i].frac];
          if (size) {
            data_memcpy_reduce.type = ememcpy;
            data_memcpy_reduce.buffer_type1 = eshmemo;
            data_memcpy_reduce.buffer_number1 = 0;
            data_memcpy_reduce.is_offset1 = 0;
            data_memcpy_reduce.offset1 = add;
            data_memcpy_reduce.buffer_type2 = esendbufp;
            data_memcpy_reduce.buffer_number2 = 0;
            data_memcpy_reduce.is_offset2 = 0;
            data_memcpy_reduce.offset2 = add2;
            data_memcpy_reduce.size = size;
            nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
          }
          add += size;
        }
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
        size = moffsets[num_nodes];
        add = 0;
        if (size <= CACHE_LINE_SIZE) {
          for (i = 1; i < node_size / counts_max; i++) {
            if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
              add2 = add + CACHE_LINE_SIZE * i;
            } else {
              add2 = add + moffsets[num_nodes] * i;
            }
            if (size) {
              if (node_rank == 0) {
                data_memcpy_reduce.type = esreduce;
                data_memcpy_reduce.buffer_type1 = eshmemo;
                data_memcpy_reduce.buffer_number1 = 0;
                data_memcpy_reduce.is_offset1 = 0;
                data_memcpy_reduce.offset1 = add;
                data_memcpy_reduce.buffer_type2 = eshmemo;
                data_memcpy_reduce.buffer_number2 = 0;
                data_memcpy_reduce.is_offset2 = 0;
                data_memcpy_reduce.offset2 = add2;
                data_memcpy_reduce.size = size;
                nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
              } else {
                data_memcpy_reduce.type = esreduc_;
                data_memcpy_reduce.buffer_type1 = eshmemo;
                data_memcpy_reduce.buffer_number1 = 0;
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
          }
          //        nbuffer_out+=sprintf(buffer_out+nbuffer_out, "
          //        SOCKET_BARRIER_MASTER\n");
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
        } else {
          for (i = 1; i < node_size / counts_max; i++) {
            if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
              add2 = add + CACHE_LINE_SIZE * i;
            } else {
              add2 = add + moffsets[num_nodes] * i;
            }
            if (size) {
              if (node_rank == 0) {
                data_memcpy_reduce.type = ereduce;
                data_memcpy_reduce.buffer_type1 = eshmemo;
                data_memcpy_reduce.buffer_number1 = 0;
                data_memcpy_reduce.is_offset1 = 0;
                data_memcpy_reduce.offset1 = add;
                data_memcpy_reduce.buffer_type2 = eshmemo;
                data_memcpy_reduce.buffer_number2 = 0;
                data_memcpy_reduce.is_offset2 = 0;
                data_memcpy_reduce.offset2 = add2;
                data_memcpy_reduce.size = size;
                nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
              } else {
                data_memcpy_reduce.type = ereduc_;
                data_memcpy_reduce.buffer_type1 = eshmemo;
                data_memcpy_reduce.buffer_number1 = 0;
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
          }
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
        }
        break;
      case 1:
        //        if (collective_type==2){
        //          printf("copyin not implemented\n");
        //          exit(2);
        //        }
        k = (node_size / counts_max) / size_level1[0];
        if ((k < 1) || ((node_size / counts_max) % size_level1[0] != 0)) {
          add = ldispls[lrank_column];
          for (i = 0; i < size_level1[0]; i++) {
            size = mcounts[data.blocks[0].lines[i].frac];
            add2 = moffsets[data.blocks[0].lines[i].frac];
            if ((node_size / counts_max + i - lrank_row) %
                    (node_size / counts_max) ==
                0) {
              if (size) {
                data_memcpy_reduce.type = ememcpy;
                data_memcpy_reduce.buffer_type1 = eshmemo;
                data_memcpy_reduce.buffer_number1 = 0;
                data_memcpy_reduce.is_offset1 = 0;
                data_memcpy_reduce.offset1 = add;
                data_memcpy_reduce.buffer_type2 = esendbufp;
                data_memcpy_reduce.buffer_number2 = 0;
                data_memcpy_reduce.is_offset2 = 0;
                data_memcpy_reduce.offset2 = add2;
                data_memcpy_reduce.size = size;
                nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
              }
            }
            add += size;
          }
          for (i = 1; i < node_size / counts_max; i++) {
            nbuffer_out += ext_mpi_write_assembler_line(
                buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
            add = ldispls[lrank_column];
            for (j = 0; j < data.blocks[0].num_lines; j++) {
              size = mcounts[data.blocks[0].lines[j].frac];
              add2 = moffsets[data.blocks[0].lines[j].frac];
              if ((node_size / counts_max + j - lrank_row) %
                      (node_size / counts_max) ==
                  i) {
                if (size) {
                  data_memcpy_reduce.type = ereduce;
                  data_memcpy_reduce.buffer_type1 = eshmemo;
                  data_memcpy_reduce.buffer_number1 = 0;
                  data_memcpy_reduce.is_offset1 = 0;
                  data_memcpy_reduce.offset1 = add;
                  data_memcpy_reduce.buffer_type2 = esendbufp;
                  data_memcpy_reduce.buffer_number2 = 0;
                  data_memcpy_reduce.is_offset2 = 0;
                  data_memcpy_reduce.offset2 = add2;
                  data_memcpy_reduce.size = size;
                  nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
                }
              }
              add += size;
            }
          }
        } else {
          add = ldispls[lrank_column];
          for (i = 0; i < data.blocks[0].num_lines; i++) {
            size = mcounts[data.blocks[0].lines[i].frac];
            add2 = moffsets[data.blocks[0].lines[i].frac];
            for (m = 0; m < k; m++) {
              size_local = (size / type_size / k) * type_size;
              add_local = size_local * m;
              if (m < (size / type_size) % k) {
                size_local += type_size;
                add_local += m * type_size;
              } else {
                add_local += ((size / type_size) % k) * type_size;
              }
              if ((lrank_row < k * size_level1[0]) &&
                  ((node_size / counts_max + m - lrank_row % k) % k == 0) &&
                  ((node_size / counts_max + i - lrank_row / k) %
                       size_level1[0] ==
                   0)) {
                if (size_local) {
                  data_memcpy_reduce.type = ememcpy;
                  data_memcpy_reduce.buffer_type1 = eshmemo;
                  data_memcpy_reduce.buffer_number1 = 0;
                  data_memcpy_reduce.is_offset1 = 0;
                  data_memcpy_reduce.offset1 = add + add_local;
                  data_memcpy_reduce.buffer_type2 = esendbufp;
                  data_memcpy_reduce.buffer_number2 = 0;
                  data_memcpy_reduce.is_offset2 = 0;
                  data_memcpy_reduce.offset2 = add2 + add_local;
                  data_memcpy_reduce.size = size_local;
                  nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
                }
              }
            }
            add += size;
          }
          for (i = 1; i < node_size / counts_max; i++) {
            nbuffer_out += ext_mpi_write_assembler_line(
                buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
            add = ldispls[lrank_column];
            for (j = 0; j < data.blocks[0].num_lines; j++) {
              size = mcounts[data.blocks[0].lines[j].frac];
              add2 = moffsets[data.blocks[0].lines[j].frac];
              for (m = 0; m < k; m++) {
                size_local = (size / type_size / k) * type_size;
                add_local = size_local * m;
                if (m < (size / type_size) % k) {
                  size_local += type_size;
                  add_local += m * type_size;
                } else {
                  add_local += ((size / type_size) % k) * type_size;
                }
                if ((lrank_row < k * size_level1[0]) &&
                    ((node_size / counts_max + m - lrank_row % k) % k ==
                     i % k) &&
                    ((node_size / counts_max + j - lrank_row / k) %
                         size_level1[0] ==
                     i / k)) {
                  if (size_local) {
                    data_memcpy_reduce.type = ereduce;
                    data_memcpy_reduce.buffer_type1 = eshmemo;
                    data_memcpy_reduce.buffer_number1 = 0;
                    data_memcpy_reduce.is_offset1 = 0;
                    data_memcpy_reduce.offset1 = add + add_local;
                    data_memcpy_reduce.buffer_type2 = esendbufp;
                    data_memcpy_reduce.buffer_number2 = 0;
                    data_memcpy_reduce.is_offset2 = 0;
                    data_memcpy_reduce.offset2 = add2 + add_local;
                    data_memcpy_reduce.size = size_local;
                    nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
                  }
                }
              }
              add += size;
            }
          }
        }
        nbuffer_out += ext_mpi_write_assembler_line(
            buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
        break;
      case 2:
        if (lrank_row >= node_row_size / 2){
          if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
            add = CACHE_LINE_SIZE * (lrank_row - node_row_size / 2) + ldispls[lrank_column];
          } else {
            add = moffsets[num_nodes] * (lrank_row - node_row_size / 2) + ldispls[lrank_column];
          }
          for (i = 0; i < data.blocks[0].num_lines; i++) {
            size = mcounts[data.blocks[0].lines[i].frac];
            add2 = moffsets[data.blocks[0].lines[i].frac];
            if (size) {
              data_memcpy_reduce.type = ememcpy;
              data_memcpy_reduce.buffer_type1 = eshmemo;
              data_memcpy_reduce.buffer_number1 = 0;
              data_memcpy_reduce.is_offset1 = 0;
              data_memcpy_reduce.offset1 = add;
              data_memcpy_reduce.buffer_type2 = esendbufp;
              data_memcpy_reduce.buffer_number2 = 0;
              data_memcpy_reduce.is_offset2 = 0;
              data_memcpy_reduce.offset2 = add2;
              data_memcpy_reduce.size = size;
              nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
            }
            add += size;
          }
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "sd", eset_socket_barrier, lrank_row);
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "sd", ewait_socket_barrier, lrank_row - node_row_size / 2);
        } else {
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "sd", eset_socket_barrier, lrank_row);
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "sd", ewait_socket_barrier, lrank_row + node_row_size / 2);
          if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
            add = CACHE_LINE_SIZE * lrank_row + ldispls[lrank_column];
          } else {
            add = moffsets[num_nodes] * lrank_row + ldispls[lrank_column];
          }
          for (i = 0; i < data.blocks[0].num_lines; i++) {
            size = mcounts[data.blocks[0].lines[i].frac];
            add2 = moffsets[data.blocks[0].lines[i].frac];
            if (size) {
              data_memcpy_reduce.type = ereduce;
              data_memcpy_reduce.buffer_type1 = eshmemo;
              data_memcpy_reduce.buffer_number1 = 0;
              data_memcpy_reduce.is_offset1 = 0;
              data_memcpy_reduce.offset1 = add;
              data_memcpy_reduce.buffer_type2 = esendbufp;
              data_memcpy_reduce.buffer_number2 = 0;
              data_memcpy_reduce.is_offset2 = 0;
              data_memcpy_reduce.offset2 = add2;
              data_memcpy_reduce.size = size;
              nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
            }
            add += size;
          }
        }
        size = moffsets[num_nodes];
        for (step = node_row_size / 4, i = 1; step >= 1; step /= 2, i++) {
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "sd", eset_socket_barrier, lrank_row + i * node_row_size);
          if (lrank_row % (step * 2) < step) {
            nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "sd", ewait_socket_barrier, lrank_row + step + i * node_row_size);
          } else {
            nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "sd", ewait_socket_barrier, lrank_row - step + i * node_row_size);
          }
          if (lrank_row < step) {
            if (size <= CACHE_LINE_SIZE) {
              add = CACHE_LINE_SIZE * lrank_row;
              add2 = add + CACHE_LINE_SIZE * step;
            } else {
              add = size * lrank_row;
              add2 = add + size * step;
            }
            if (size) {
              data_memcpy_reduce.type = esreduce;
              data_memcpy_reduce.buffer_type1 = eshmemo;
              data_memcpy_reduce.buffer_number1 = 0;
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
        }
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", enext_socket_barrier);
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
        add=add2=0;
        size=moffsets[num_nodes];
        if (size <= CACHE_LINE_SIZE) size = CACHE_LINE_SIZE;
        size *= node_row_size;
        add=add2=size;
        size=0;
        data_memcpy_reduce.type = ememcp_;
        data_memcpy_reduce.buffer_type1 = eshmemo;
        data_memcpy_reduce.buffer_number1 = 0;
        data_memcpy_reduce.is_offset1 = 0;
        data_memcpy_reduce.offset1 = add;
        data_memcpy_reduce.buffer_type2 = eshmemo;
        data_memcpy_reduce.buffer_number2 = 0;
        data_memcpy_reduce.is_offset2 = 0;
        data_memcpy_reduce.offset2 = add2;
        data_memcpy_reduce.size = size;
        nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
        break;
      case 3:
	tsize = 8;
	for (i = 0; i < size_level1[0]; i++) {
	  tsize += mcounts[data.blocks[0].lines[i].frac];
	}
        add = node_rank * CACHE_LINE_SIZE + CACHE_LINE_SIZE - tsize;
        for (i = 0; i < data.blocks[0].num_lines; i++) {
          size = mcounts[data.blocks[0].lines[i].frac];
          add2 = moffsets[data.blocks[0].lines[i].frac];
          if (size) {
            data_memcpy_reduce.type = esmemcpy;
            data_memcpy_reduce.buffer_type1 = eshmemo;
            data_memcpy_reduce.buffer_number1 = 0;
            data_memcpy_reduce.is_offset1 = 1;
            data_memcpy_reduce.offset_number1 = -1;
            data_memcpy_reduce.offset1 = add;
            data_memcpy_reduce.buffer_type2 = esendbufp;
            data_memcpy_reduce.buffer_number2 = 0;
            data_memcpy_reduce.is_offset2 = 0;
            data_memcpy_reduce.offset2 = add2;
            data_memcpy_reduce.size = size;
            nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
          }
          add += size;
        }
        for (barriers_size = 0, step = 1; step < node_size; barriers_size++, step <<= 1) {
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "ssdsdsd", eset_mem, eshmemo, 0, ecpbuffer_offseto, -1, ecp, (barriers_size * node_size + node_rank) * CACHE_LINE_SIZE + CACHE_LINE_SIZE - 1);
          nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "ssdsdsd", eunset_mem, eshmemo, 0, ecpbuffer_offseto, -1, ecp, (barriers_size * node_size + (node_rank + step) % node_size) * CACHE_LINE_SIZE + CACHE_LINE_SIZE - 1);
          if (!(node_rank % (step << 1))) {
            add = ((barriers_size + 1) * node_size + node_rank) * CACHE_LINE_SIZE + CACHE_LINE_SIZE - tsize;
            add2 = (barriers_size * node_size + node_rank) * CACHE_LINE_SIZE + CACHE_LINE_SIZE - tsize;
            j = -1;
            if ((step << 1 >= node_size) && (node_rank == 0)){
              add = 0;
              j = 0;
            }
            for (i = 0; i < data.blocks[0].num_lines; i++) {
              size = mcounts[data.blocks[0].lines[i].frac];
              if (size) {
                data_memcpy_reduce.type = esmemcpy;
                data_memcpy_reduce.buffer_type1 = eshmemo;
                data_memcpy_reduce.buffer_number1 = 0;
                data_memcpy_reduce.is_offset1 = 1;
                data_memcpy_reduce.offset_number1 = j;
                data_memcpy_reduce.offset1 = add;
                data_memcpy_reduce.buffer_type2 = eshmemo;
                data_memcpy_reduce.buffer_number2 = 0;
                data_memcpy_reduce.is_offset2 = 1;
                data_memcpy_reduce.offset_number2 = -1;
                data_memcpy_reduce.offset2 = add2;
                data_memcpy_reduce.size = size;
                nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
              }
              add += size;
	      add2 += size;
            }
            add = ((barriers_size + 1) * node_size + node_rank) * CACHE_LINE_SIZE + CACHE_LINE_SIZE - tsize;
            add2 = (barriers_size * node_size + (node_rank + step) % node_size) * CACHE_LINE_SIZE + CACHE_LINE_SIZE - tsize;
            j = -1;
            if ((step << 1 >= node_size) && (node_rank == 0)){
              add = 0;
              j = 0;
            }
            if (node_rank + step < node_size) {
              for (i = 0; i < size_level1[0]; i++) {
                size = mcounts[data.blocks[0].lines[i].frac];
                if (size) {
                  data_memcpy_reduce.type = esreduce;
                  data_memcpy_reduce.buffer_type1 = eshmemo;
                  data_memcpy_reduce.buffer_number1 = 0;
                  data_memcpy_reduce.is_offset1 = 1;
                  data_memcpy_reduce.offset_number1 = j;
                  data_memcpy_reduce.offset1 = add;
                  data_memcpy_reduce.buffer_type2 = eshmemo;
                  data_memcpy_reduce.buffer_number2 = 0;
                  data_memcpy_reduce.is_offset2 = 1;
                  data_memcpy_reduce.offset_number2 = -1;
                  data_memcpy_reduce.offset2 = add2;
                  data_memcpy_reduce.size = size;
                  nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
                }
                add += size;
	        add2 += size;
              }
            }
          }
        }
        nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
        size=moffsets[num_nodes];
        if (size <= CACHE_LINE_SIZE) size = CACHE_LINE_SIZE + CACHE_LINE_SIZE - tsize;
        size *= node_row_size;
        add=add2=size;
        size=0;
        data_memcpy_reduce.type = ememcp_;
        data_memcpy_reduce.buffer_type1 = eshmemo;
        data_memcpy_reduce.buffer_number1 = 0;
        data_memcpy_reduce.is_offset1 = 0;
        data_memcpy_reduce.offset1 = add;
        data_memcpy_reduce.buffer_type2 = eshmemo;
        data_memcpy_reduce.buffer_number2 = 0;
        data_memcpy_reduce.is_offset2 = 0;
        data_memcpy_reduce.offset2 = add2;
        data_memcpy_reduce.size = size;
        nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
        break;
      case 4:
        nbuffer_out += copyin(parameters, data, type_size, parameters->socket_row_size, lrank_row, 0, buffer_out+nbuffer_out);
/*        nbuffer_out += copyin(parameters, data, size_level0, size_level1, type_size, 2, lrank_row%2, lrank_row/2, buffer_out+nbuffer_out);
        block_offsets=(int*)malloc(2*sizeof(int));
        for (i=2; i<parameters->node_row_size; i*=2){
          block_offsets[0]=(lrank_row/2/i)*i;
          block_offsets[1]=(lrank_row/2/i)*i+i/2;
          nbuffer_out += reduce(parameters, data, type_size, i*2, lrank_row%(i*2), 2, block_offsets, buffer_out+nbuffer_out);
          nbuffer_out += write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
        }
        free(block_offsets);*/
        for (i = 0; i < size_level1[0]; i++) {
          total_message_size+=parameters->message_sizes[i];
        }
        add=add2=total_message_size*(parameters->socket_row_size/2-1);
        size=total_message_size;
        data_memcpy_reduce.type = ememcp_;
        data_memcpy_reduce.buffer_type1 = eshmemo;
        data_memcpy_reduce.buffer_number1 = 0;
        data_memcpy_reduce.is_offset1 = 0;
        data_memcpy_reduce.offset1 = add;
        data_memcpy_reduce.buffer_type2 = eshmemo;
        data_memcpy_reduce.buffer_number2 = 0;
        data_memcpy_reduce.is_offset2 = 0;
        data_memcpy_reduce.offset2 = add2;
        data_memcpy_reduce.size = size;
        nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
        break;
      case 5:
        if (collective_type == 2) {
          printf("copyin not implemented\n");
          exit(2);
        }
        if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
          add = CACHE_LINE_SIZE * lrank_row + ldispls[lrank_column];
        } else {
          add = moffsets[num_nodes] * lrank_row + ldispls[lrank_column];
        }
        for (i = 0; i < data.blocks[0].num_lines; i++) {
          size = mcounts[data.blocks[0].lines[i].frac];
          add2 = moffsets[data.blocks[0].lines[i].frac];
          if (size) {
            nbuffer_out +=
                sprintf(buffer_out + nbuffer_out,
                        " MEMCPY SHMEM+ %d SENDBUF+ %d %d\n", add, add2, size);
          }
          add += size;
        }
        if (node_rank % RADIX) {
          nbuffer_out +=
              sprintf(buffer_out + nbuffer_out, " SETTOONE SHMEM+ %d 1\n",
                      -CACHE_LINE_SIZE * (1 + node_rank));
        }
        size = moffsets[num_nodes];
        for (barriers_size = 1, step = RADIX; step / RADIX < node_size;
             barriers_size++, step *= RADIX) {
          for (substep = 0; substep < RADIX - 1; substep++) {
            if ((node_rank % step == 0) &&
                (node_rank + step / RADIX + substep * (step / RADIX) <
                 node_size / counts_max)) {
              nbuffer_out +=
                  sprintf(buffer_out + nbuffer_out, " MEMWAIT SHMEM+ %d 1\n",
                          -CACHE_LINE_SIZE * (1 + node_rank + step / RADIX +
                                              substep * (step / RADIX) +
                                              (barriers_size - 1) * node_size));
            }
            if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
              add = CACHE_LINE_SIZE * node_rank;
              add2 = add + CACHE_LINE_SIZE *
                               (step / RADIX + substep * (step / RADIX));
            } else {
              add = moffsets[num_nodes] * node_rank;
              add2 = add + moffsets[num_nodes] *
                               (step / RADIX + substep * (step / RADIX));
            }
            if (size) {
              if ((node_rank % step == 0) &&
                  (node_rank + step / RADIX + substep * (step / RADIX) <
                   node_size / counts_max)) {
                nbuffer_out += sprintf(buffer_out + nbuffer_out,
                                       " SREDUCE SHMEM+ %d SHMEM+ %d %d\n", add,
                                       add2, size);
              }
              if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
                add2 = add + CACHE_LINE_SIZE * (node_size - 1);
              } else {
                add2 = add + moffsets[num_nodes] * (node_size - 1);
              }
              nbuffer_out +=
                  sprintf(buffer_out + nbuffer_out,
                          " SREDUC_ SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
            }
          }
          if (step < node_size) {
            nbuffer_out += sprintf(
                buffer_out + nbuffer_out, " SETTOONE SHMEM+ %d 1\n",
                -CACHE_LINE_SIZE * (1 + node_rank + barriers_size * node_size));
          }
        }
        nbuffer_out += sprintf(buffer_out + nbuffer_out, " SOCKET_BARRIER\n");
        for (barriers_size -= 2; barriers_size >= 0; barriers_size--) {
          nbuffer_out += sprintf(
              buffer_out + nbuffer_out, " SETTOZERO SHMEM+ %d 1\n",
              -CACHE_LINE_SIZE * (1 + node_rank + barriers_size * node_size));
        }
        break;
      case 6:
        if (collective_type == 2) {
          printf("copyin not implemented\n");
          exit(2);
        }
        if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
          add = CACHE_LINE_SIZE * lrank_row + ldispls[lrank_column];
        } else {
          add = moffsets[num_nodes] * lrank_row + ldispls[lrank_column];
        }
        for (i = 0; i < data.blocks[0].num_lines; i++) {
          size = mcounts[data.blocks[0].lines[i].frac];
          add2 = moffsets[data.blocks[0].lines[i].frac];
          if (size) {
            nbuffer_out +=
                sprintf(buffer_out + nbuffer_out,
                        " MEMCPY SHMEM+ %d SENDBUF+ %d %d\n", add, add2, size);
          }
          add += size;
        }
        if (node_rank % RADIX) {
          nbuffer_out +=
              sprintf(buffer_out + nbuffer_out, " SETTOONE SHMEM+ %d 1\n",
                      -CACHE_LINE_SIZE * (1 + node_rank));
        }
        size = moffsets[num_nodes];
        nbuffer_out += sprintf(buffer_out + nbuffer_out,
                               " COPYIN %d %d %d %d %d\n", node_size, node_rank,
                               counts_max, moffsets[num_nodes], RADIX);
        for (barriers_size = 1, step = RADIX; step / RADIX < node_size;
             barriers_size++, step *= RADIX) {
          for (substep = 0; substep < RADIX - 1; substep++) {
            if ((node_rank % step == 0) &&
                (node_rank + step / RADIX + substep * (step / RADIX) <
                 node_size / counts_max)) {
              //                nbuffer_out+=sprintf(buffer_out+nbuffer_out, "
              //                MEMWAIT SHMEM+ %d 1\n",
              //                -CACHE_LINE_SIZE*(1+node_rank+step/RADIX+substep*(step/RADIX)+(barriers_size-1)*node_size));
            }
            if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
              add = CACHE_LINE_SIZE * node_rank;
              add2 = add + CACHE_LINE_SIZE *
                               (step / RADIX + substep * (step / RADIX));
            } else {
              add = moffsets[num_nodes] * node_rank;
              add2 = add + moffsets[num_nodes] *
                               (step / RADIX + substep * (step / RADIX));
            }
            if (size) {
              if ((node_rank % step == 0) &&
                  (node_rank + step / RADIX + substep * (step / RADIX) <
                   node_size / counts_max)) {
                //                  nbuffer_out+=sprintf(buffer_out+nbuffer_out,
                //                  " SREDUCE SHMEM+ %d SHMEM+ %d %d\n", add,
                //                  add2, size);
              }
              if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
                add2 = add + CACHE_LINE_SIZE * (node_size - 1);
              } else {
                add2 = add + moffsets[num_nodes] * (node_size - 1);
              }
              nbuffer_out +=
                  sprintf(buffer_out + nbuffer_out,
                          " SREDUC_ SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
            }
          }
          if (step < node_size) {
            //              nbuffer_out+=sprintf(buffer_out+nbuffer_out, "
            //              SETTOONE SHMEM+ %d 1\n",
            //              -CACHE_LINE_SIZE*(1+node_rank+barriers_size*node_size));
          }
        }
        nbuffer_out += sprintf(buffer_out + nbuffer_out, " SOCKET_BARRIER\n");
        for (barriers_size -= 2; barriers_size >= 0; barriers_size--) {
          nbuffer_out += sprintf(
              buffer_out + nbuffer_out, " SETTOZERO SHMEM+ %d 1\n",
              -CACHE_LINE_SIZE * (1 + node_rank + barriers_size * node_size));
        }
        break;
      case 7:
        if (collective_type == 2) {
          printf("copyin not implemented\n");
          exit(2);
        }
        if (lrank_row == 0) {
          add = ldispls[lrank_column];
          for (i = 0; i < data.blocks[0].num_lines; i++) {
            size = mcounts[data.blocks[0].lines[i].frac];
            add2 = moffsets[data.blocks[0].lines[i].frac];
            if (size) {
              nbuffer_out += sprintf(buffer_out + nbuffer_out,
                                     " MEMCPY SHMEM+ %d SENDBUF+ %d %d\n", add,
                                     add2, size);
            }
            add += size;
          }
        }
        nbuffer_out += sprintf(buffer_out + nbuffer_out, " SOCKET_BARRIER\n");
        if (lrank_row != 0) {
          add = ldispls[lrank_column];
          for (i = 0; i < data.blocks[0].num_lines; i++) {
            size = mcounts[data.blocks[0].lines[i].frac];
            add2 = moffsets[data.blocks[0].lines[i].frac];
            if (size) {
              nbuffer_out += sprintf(buffer_out + nbuffer_out,
                                     " ATOMICADD SHMEM+ %d SENDBUF+ %d %d\n",
                                     add, add2, size);
            }
            add += size;
          }
        }
        nbuffer_out += sprintf(buffer_out + nbuffer_out, " SOCKET_BARRIER\n");
        break;
      case 8:
        if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
          add = CACHE_LINE_SIZE * lrank_row + ldispls[lrank_column];
          if (moffsets[num_nodes] < CACHE_LINE_SIZE - 2 * type_size) {
            add = -CACHE_LINE_SIZE * (1 + node_rank) + type_size;
          }
        } else {
          add = moffsets[num_nodes] * lrank_row + ldispls[lrank_column];
        }
        for (i = 0; i < data.blocks[0].num_lines; i++) {
          size = mcounts[data.blocks[0].lines[i].frac];
          add2 = moffsets[data.blocks[0].lines[i].frac];
          if (size) {
            nbuffer_out +=
                sprintf(buffer_out + nbuffer_out,
                        " MEMCPY SHMEM+ %d SENDBUF+ %d %d\n", add, add2, size);
            if (node_rank != 0) {
              nbuffer_out +=
                  sprintf(buffer_out + nbuffer_out, " SETTOONE SHMEM+ %d 1\n",
                          -CACHE_LINE_SIZE * (1 + node_rank));
            }
          }
          add += size;
        }
        size = moffsets[num_nodes];
        add = 0;
        for (i = 1; i < node_size / counts_max; i++) {
          if (moffsets[num_nodes] < CACHE_LINE_SIZE) {
            add2 = add + CACHE_LINE_SIZE * i;
            if (moffsets[num_nodes] < CACHE_LINE_SIZE - 2 * type_size) {
              add2 = -CACHE_LINE_SIZE * (1 + i) + type_size;
            }
          } else {
            add2 = add + moffsets[num_nodes] * i;
          }
          if (size) {
            if (node_rank == 0) {
              nbuffer_out +=
                  sprintf(buffer_out + nbuffer_out, " MEMWAIT SHMEM+ %d 1\n",
                          -CACHE_LINE_SIZE * (1 + i));
              nbuffer_out +=
                  sprintf(buffer_out + nbuffer_out,
                          " SREDUCE SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
              nbuffer_out +=
                  sprintf(buffer_out + nbuffer_out, " SETTOZERO SHMEM+ %d 1\n",
                          -CACHE_LINE_SIZE * (1 + i));
              nbuffer_out += sprintf(buffer_out + nbuffer_out,
                                     " SETTOZERO SHMEM+ %d 1\n", add2);
            } else {
              nbuffer_out +=
                  sprintf(buffer_out + nbuffer_out,
                          " SREDUC_ SHMEM+ %d SHMEM+ %d %d\n", add, add2, size);
            }
          }
        }
        nbuffer_out +=
            sprintf(buffer_out + nbuffer_out, " SOCKET_BARRIER_MASTER\n");
        break;
      }
    }
  } else {
    add = iodispls[node_rank];
    k = 1;
    for (i = 0; (i < data.blocks[0].num_lines) && k; i++) {
      for (j = 0; j < data.blocks[0].lines[i].recvfrom_max; j++){
        if (data.blocks[0].lines[i].recvfrom_node[j] == -1){
          k = 0;
        }
      }
      if (k){
        size = mcounts[data.blocks[0].lines[i].frac];
        add += size;
      }
    }
    add2 = 0;
    size = iocounts[node_rank];
    if (size) {
      data_memcpy_reduce.type = ememcpy;
      data_memcpy_reduce.buffer_type1 = eshmemo;
      data_memcpy_reduce.buffer_number1 = 0;
      data_memcpy_reduce.is_offset1 = 0;
      data_memcpy_reduce.offset1 = add;
      data_memcpy_reduce.buffer_type2 = esendbufp;
      data_memcpy_reduce.buffer_number2 = 0;
      data_memcpy_reduce.is_offset2 = 0;
      data_memcpy_reduce.offset2 = add2;
      data_memcpy_reduce.size = size;
      nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
    }
    nbuffer_out += ext_mpi_write_assembler_line(buffer_out + nbuffer_out, parameters->ascii_out, "s", esocket_barrier);
  }
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
