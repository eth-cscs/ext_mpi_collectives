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
    case data_type_long_int:
    case data_type_double:
      type_size = 8;
    default:
      type_size = 1;
  }
  counts = (int *)malloc(parameters->num_sockets_per_node * sizeof(int));
  ranks = (int*)malloc(parameters->socket_row_size * parameters->num_sockets_per_node * sizeof(int));
  ranks_inv = (int*)malloc(parameters->socket_row_size * parameters->num_sockets_per_node * sizeof(int));
  countsa = (int*)malloc(parameters->socket_row_size * parameters->num_sockets_per_node * sizeof(int));
  displsa = (int*)malloc((parameters->socket_row_size * parameters->num_sockets_per_node + 1) * sizeof(int));
  for (i = 0; i < parameters->socket_row_size; i++) {
    ranks[i] = i;
  }
  lrank_row = parameters->socket_rank + parameters->socket_number * parameters->socket_row_size;
  if (parameters->copyin_method == 6) {
    for (i = 1; parameters->copyin_factors[i] < 0; i++);
    ext_mpi_rank_order(parameters->socket_row_size, i - 1, parameters->copyin_factors + 1, ranks);
    for (i = 0; i < parameters->socket_row_size; i++) {
      if (ranks[i] == lrank_row) {
        lrank_row = i;
        break;
      }
    }
  }
  for (i = 0; i < parameters->socket_row_size; i++) {
    ranks_inv[ranks[i]] = i;
  }
  if (parameters->copyin_factors[0] < 0) {
    ext_mpi_sizes_displs(parameters->socket_row_size * parameters->num_sockets_per_node, buffer_in_size, type_size, -parameters->copyin_factors[0], countsa, displsa);
  } else {
    ext_mpi_sizes_displs(parameters->socket_row_size * parameters->num_sockets_per_node, buffer_in_size, type_size, 0, countsa, displsa);
  }
  for (i = 0; i < parameters->num_sockets_per_node; i++) {
    counts[i] = 0;
  }
  for (j = 0; j < parameters->num_sockets_per_node; j++) {
    for (i = 0; i < parameters->socket_row_size; i++) {
      if (i / (parameters->socket_row_size / parameters->num_sockets_per_node) == j) {
        counts[(parameters->num_sockets_per_node - j + parameters->socket_number) % parameters->num_sockets_per_node] += countsa[ranks_inv[i]];
      }
    }
  }
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
        if ((estring1 == ememcpy) || (estring1 == ememcp_) || (estring1 == ereduce) || (estring1 == ereduc_)) {
          if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
            if (data_memcpy_reduce.buffer_type1==esendbufp||data_memcpy_reduce.buffer_type1==erecvbufp||data_memcpy_reduce.buffer_type2==esendbufp||data_memcpy_reduce.buffer_type2==erecvbufp){
              flag = 0;
	    } else if (data_memcpy_reduce.buffer_type1 == eshmem_tempp) {
	      data_memcpy_reduce.buffer_type1 = eshmemo;
	    } else if (data_memcpy_reduce.buffer_type2 == eshmem_tempp) {
	      data_memcpy_reduce.buffer_type2 = eshmemo;
            } else {
              if ((data_memcpy_reduce.buffer_type1==eshmemo)&&(data_memcpy_reduce.offset1+data_memcpy_reduce.size<=buffer_in_size)){
                if (parameters->socket_row_size == 1 && parameters->num_sockets_per_node == 1) {
		  data_memcpy_reduce.buffer_type1 = esendbufp;
		} else if (copyin7) {
		  data_memcpy_reduce.offset1 += buffer_socket_offset;
		}
                flag = 2;
              } else if ((data_memcpy_reduce.buffer_type1==eshmemo)&&(data_memcpy_reduce.offset1+data_memcpy_reduce.size<=2*buffer_in_size)){
                data_memcpy_reduce.buffer_type1 = erecvbufp;
                if (big_recvbuf && !copyin7) {
                  data_memcpy_reduce.offset1 -= buffer_in_size;
		} else if (copyin7) {
                  data_memcpy_reduce.offset1 += buffer_socket_offset;
		} else {
		  data_memcpy_reduce.offset1 = 0;
                }
                flag = 2;
              } else if (data_memcpy_reduce.buffer_type1==eshmemo) {
		if (copyin7) {
		  data_memcpy_reduce.offset1 += buffer_in_size - buffer_socket_size;
		}
	      }
              if ((data_memcpy_reduce.buffer_type2==eshmemo)&&(data_memcpy_reduce.offset2+data_memcpy_reduce.size<=buffer_in_size)){
                if (parameters->socket_row_size == 1 && parameters->num_sockets_per_node == 1) {
                  data_memcpy_reduce.buffer_type2 = esendbufp;
		} else if (copyin7) {
		  data_memcpy_reduce.offset2 += buffer_socket_offset;
		}
                flag = 2;
              } else if ((data_memcpy_reduce.buffer_type2==eshmemo)&&(data_memcpy_reduce.offset2+data_memcpy_reduce.size<=2*buffer_in_size)){
                data_memcpy_reduce.buffer_type2 = erecvbufp;
                if (big_recvbuf && !copyin7) {
                  data_memcpy_reduce.offset2 -= buffer_in_size;
		} else if (7) {
                  data_memcpy_reduce.offset2 += buffer_socket_offset;
		} else {
		  data_memcpy_reduce.offset2 = 0;
                }
                flag = 2;
	      } else if (data_memcpy_reduce.buffer_type2==eshmemo) {
                if (copyin7) {
                  data_memcpy_reduce.offset2 += buffer_in_size - buffer_socket_size;
                }
              }
              if (flag == 2){
                nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
                flag = 0;
	      }
            }
          }
        }else if ((estring1 == eirecv) || (estring1 == eirec_) || (estring1 == eisend) || (estring1 == eisen_)){
          if (ext_mpi_read_irecv_isend(line, &data_irecv_isend) >= 0) {
            if ((data_irecv_isend.buffer_type==eshmemo)&&(data_irecv_isend.offset+data_irecv_isend.size<=buffer_in_size)){
              if (parameters->socket_row_size == 1 && parameters->num_sockets_per_node == 1) {
                data_irecv_isend.buffer_type = esendbufp;
	      } else if (copyin7) {
		data_irecv_isend.offset += buffer_socket_offset;
	      }
              nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
              flag = 0;
            } else if ((data_irecv_isend.buffer_type==eshmemo)&&(data_irecv_isend.offset+data_irecv_isend.size<=2*buffer_in_size)){
              data_irecv_isend.buffer_type=erecvbufp;
              if (big_recvbuf) {
                data_irecv_isend.offset -= buffer_in_size;
	      } else if (big_recvbuf && !copyin7) {
		data_irecv_isend.offset += buffer_socket_offset;
	      } else {
                data_irecv_isend.offset = 0;
              }
              nbuffer_out += ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
              flag = 0;
	    } else if (data_irecv_isend.buffer_type==eshmemo) {
	      if (copyin7) {
                data_irecv_isend.offset += buffer_in_size - buffer_socket_size;
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
