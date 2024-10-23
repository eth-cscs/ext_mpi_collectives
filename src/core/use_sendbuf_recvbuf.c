#include "use_sendbuf_recvbuf.h"
#include "read_write.h"
#include "reduce_copyin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_use_sendbuf_recvbuf(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce;
  struct line_irecv_isend data_irecv_isend;
  int buffer_in_size = 0, nbuffer_in = 0, nbuffer_out = 0, flag, flag3, big_recvbuf, buffer_socket_offset = 0, buffer_socket_size = 0, type_size, *counts, *countsa, *displsa, *ranks, *ranks_inv, lrank_row, copyin7, i, j;
  char line[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1;
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  for (i = 0; i < parameters->num_nodes; i++) {
    buffer_in_size += parameters->message_sizes[i];
  }
  switch (parameters->data_type) {
    case data_type_int:
    case data_type_float:
      type_size = 4;
      break;
    case data_type_long_int:
    case data_type_double:
      type_size = 8;
      break;
    default:
      type_size = 1;
  }
  counts = (int *)malloc(parameters->num_sockets_per_node * sizeof(int));
  ranks = (int*)malloc(parameters->socket_row_size * parameters->num_sockets_per_node * sizeof(int));
  ranks_inv = (int*)malloc(parameters->socket_row_size * parameters->num_sockets_per_node * sizeof(int));
  countsa = (int*)malloc(parameters->socket_row_size * parameters->num_sockets_per_node * sizeof(int));
  displsa = (int*)malloc((parameters->socket_row_size * parameters->num_sockets_per_node + 1) * sizeof(int));
  for (i = 0; i < parameters->socket_row_size * parameters->num_sockets_per_node; i++) {
    ranks[i] = i;
  }
  lrank_row = parameters->socket_rank + parameters->socket_number * parameters->socket_row_size;
  if (parameters->copyin_method == 6 || parameters->copyin_method == 7) {
    for (i = 1; parameters->copyin_factors[i] < 0; i++);
    ext_mpi_rank_order(parameters->socket_row_size * parameters->num_sockets_per_node, i - 1, parameters->copyin_factors + 1, ranks);
    for (i = 0; i < parameters->socket_row_size * parameters->num_sockets_per_node; i++) {
      if (ranks[i] == lrank_row) {
        lrank_row = i;
        break;
      }
    }
  }
  for (i = 0; i < parameters->socket_row_size * parameters->num_sockets_per_node; i++) {
    ranks_inv[ranks[i]] = i;
  }
  j = 0;
  for (i = 0; i < parameters->counts_max; i ++) {
    j += parameters->counts[i];
  }
  if (parameters->copyin_factors[0] < 0) {
    ext_mpi_sizes_displs(parameters->socket_row_size * parameters->num_sockets_per_node, j, type_size, -parameters->copyin_factors[0], countsa, displsa);
  } else {
    ext_mpi_sizes_displs(parameters->socket_row_size * parameters->num_sockets_per_node, j, type_size, 0, countsa, displsa);
  }
  for (i = 0; i < parameters->num_sockets_per_node; i++) {
    counts[i] = 0;
  }
  for (j = 0; j < parameters->num_sockets_per_node; j++) {
    for (i = 0; i < parameters->socket_row_size * parameters->num_sockets_per_node; i++) {
      if (i / parameters->socket_row_size == j) {
        counts[(parameters->num_sockets_per_node - j + parameters->socket_number) % parameters->num_sockets_per_node] += countsa[ranks_inv[i]];
      }
    }
  }
  buffer_socket_size = parameters->counts[0];
  buffer_socket_offset = 0;
  for (i = 0; i < parameters->socket_number; i++) {
//    buffer_socket_offset += counts[parameters->num_sockets_per_node - i];
//    buffer_socket_offset += counts[(parameters->num_sockets_per_node + i) % parameters->num_sockets_per_node];
    buffer_socket_offset += parameters->counts[(parameters->num_sockets_per_node + i + 1) % parameters->num_sockets_per_node];
  }
//printf("cccc %d %d %d %d %d %d %d %d %d %d %d %d %d\n", parameters->socket_number, counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7], counts[8], counts[9], counts[10], counts[11]);
//printf("bbbb %d %d %d %d %d %d %d %d %d %d %d %d %d\n", parameters->socket_number, countsa[0], countsa[1], countsa[2], countsa[3], countsa[4], countsa[5], countsa[6], countsa[7], countsa[8], countsa[9], countsa[10], countsa[11]);
//printf("aaaa %d %d %d %d\n", parameters->socket_number, buffer_socket_offset, buffer_socket_size, buffer_in_size);
  free(displsa);
  free(countsa);
  free(ranks_inv);
  free(ranks);
  free(counts);
  copyin7 = parameters->copyin_method == 7 && parameters->num_sockets_per_node > 1;
  for (i = 0; parameters->num_ports[i]; i++)
    ;
  big_recvbuf = parameters->num_ports[i - 1] > 0;
  do {
    nbuffer_in += flag3 =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag3) {
      flag = 1;
      if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
        if (estring1 == ememcpy || estring1 == ememcp_ || estring1 == ereduce || estring1 == ereduc_ || estring1 == esmemcpy || estring1 == esmemcp_ || estring1 == esreduce || estring1 == esreduc_) {
          if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
            if (data_memcpy_reduce.buffer_type1 == eshmemo && data_memcpy_reduce.offset1 + data_memcpy_reduce.size <= buffer_in_size && !copyin7){
              if (parameters->socket_row_size == 1 && parameters->num_sockets_per_node == 1) {
		data_memcpy_reduce.buffer_type1 = esendbufp;
                flag = 2;
	      }
            } else if (data_memcpy_reduce.buffer_type1 == eshmemo && data_memcpy_reduce.offset1 + data_memcpy_reduce.size <= 2 * buffer_in_size && !copyin7){
              data_memcpy_reduce.buffer_type1 = erecvbufp;
              if (big_recvbuf) {
                data_memcpy_reduce.offset1 -= buffer_in_size;
	      } else {
		data_memcpy_reduce.offset1 = 0;
              }
              flag = 2;
            } else if (copyin7) {
	      if (data_memcpy_reduce.buffer_type1 == eshmemo && !(data_memcpy_reduce.offset1 + data_memcpy_reduce.size <= buffer_in_size) && data_memcpy_reduce.offset1 + data_memcpy_reduce.size <= 2 * buffer_in_size) {
                data_memcpy_reduce.buffer_type1 = erecvbufp;
                data_memcpy_reduce.offset1 += buffer_socket_offset - buffer_in_size;
                flag = 2;
              } else if (data_memcpy_reduce.buffer_type1 == eshmemo) {
                data_memcpy_reduce.offset1 += buffer_socket_offset;
                flag = 2;
              }
	    }
            if (data_memcpy_reduce.buffer_type2 == eshmemo && data_memcpy_reduce.offset2 + data_memcpy_reduce.size <= buffer_in_size && !copyin7){
              if (parameters->socket_row_size == 1 && parameters->num_sockets_per_node == 1) {
                data_memcpy_reduce.buffer_type2 = esendbufp;
                flag = 2;
	      }
            } else if (data_memcpy_reduce.buffer_type2 == eshmemo && data_memcpy_reduce.offset2 + data_memcpy_reduce.size <= 2 * buffer_in_size && !copyin7){
              data_memcpy_reduce.buffer_type2 = erecvbufp;
              if (big_recvbuf) {
                data_memcpy_reduce.offset2 -= buffer_in_size;
	      } else {
	        data_memcpy_reduce.offset2 = 0;
              }
              flag = 2;
	    } else if (copyin7) {
	      if (data_memcpy_reduce.buffer_type2 == eshmemo && !(data_memcpy_reduce.offset2 + data_memcpy_reduce.size <= buffer_in_size) && data_memcpy_reduce.offset2 + data_memcpy_reduce.size <= 2 * buffer_in_size) {
                data_memcpy_reduce.buffer_type2 = erecvbufp;
                data_memcpy_reduce.offset2 += buffer_socket_offset - buffer_in_size;
                flag = 2;
              } else if (data_memcpy_reduce.buffer_type2 == eshmemo) {
                data_memcpy_reduce.offset2 += buffer_socket_offset;
                flag = 2;
              }
            }
	    if (data_memcpy_reduce.buffer_type1 == eshmem_tempp) {
	      data_memcpy_reduce.buffer_type1 = eshmemo;
              flag = 2;
	    }
	    if (data_memcpy_reduce.buffer_type2 == eshmem_tempp) {
	      data_memcpy_reduce.buffer_type2 = eshmemo;
              flag = 2;
	    }
            if (flag == 2){
              nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
              flag = 0;
	    }
          }
        } else if ((estring1 == eirecv) || (estring1 == eirec_) || (estring1 == eisend) || (estring1 == eisen_)){
          if (ext_mpi_read_irecv_isend(line, &data_irecv_isend) >= 0) {
            if (data_irecv_isend.buffer_type == eshmemo && data_irecv_isend.offset + data_irecv_isend.size <= buffer_in_size && !copyin7){
              if (parameters->socket_row_size == 1 && parameters->num_sockets_per_node == 1) {
                data_irecv_isend.buffer_type = esendbufp;
	      }
              nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
              flag = 0;
            } else if (data_irecv_isend.buffer_type == eshmemo && data_irecv_isend.offset + data_irecv_isend.size <= 2 * buffer_in_size && !copyin7){
              data_irecv_isend.buffer_type = erecvbufp;
              if (big_recvbuf) {
                data_irecv_isend.offset -= buffer_in_size;
	      } else {
                data_irecv_isend.offset = 0;
              }
              nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
              flag = 0;
	    } else if (copyin7) {
	      if (data_irecv_isend.buffer_type == eshmemo && !(data_irecv_isend.offset + data_irecv_isend.size <= buffer_in_size) && data_irecv_isend.offset + data_irecv_isend.size <= 2 * buffer_in_size) {
                data_irecv_isend.buffer_type = erecvbufp;
	        data_irecv_isend.offset += buffer_socket_offset - buffer_in_size;
                nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
                flag = 0;
	      } else if (data_irecv_isend.buffer_type == eshmemo) {
	        data_irecv_isend.offset += buffer_socket_offset;
                nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
                flag = 0;
	      }
            }
          }
        }
      }
      if (flag) {
        nbuffer_out +=
            ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
      }
    }
  } while (flag3);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
}
