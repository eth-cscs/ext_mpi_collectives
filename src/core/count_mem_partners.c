#include "count_mem_partners.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_count_mem_partners(char *buffer_in, char *buffer_out) {
  struct line_memcpy_reduce data_memcpy_reduce;
  int nbuffer_out = 0, nbuffer_in = 0, flag3, *partner_send, *partner_recv, i;
  char line[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1;
  memset(line, 0, 1000);
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  partner_send = (int*)malloc(sizeof(int) * parameters->socket_row_size * parameters->num_sockets_per_node);
  memset(partner_send, 0, sizeof(int) * parameters->socket_row_size * parameters->num_sockets_per_node);
  partner_recv = (int*)malloc(sizeof(int) * parameters->socket_row_size * parameters->num_sockets_per_node);
  memset(partner_recv, 0, sizeof(int) * parameters->socket_row_size * parameters->num_sockets_per_node);
  do {
    nbuffer_in += flag3 =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag3) {
      if (ext_mpi_read_assembler_line(line, 0, "s", &estring1) >= 0) {
        if ((estring1 == esmemcpy) || (estring1 == esreduce)) {
          if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
	    partner_send[data_memcpy_reduce.buffer_number1] = 1;
	    partner_recv[data_memcpy_reduce.buffer_number1] = 1;
	    partner_send[data_memcpy_reduce.buffer_number2] = 1;
	    partner_recv[data_memcpy_reduce.buffer_number2] = 1;
          }
        }
	if ((estring1 == ememcpy) || (estring1 == ereduce)) {
          if (ext_mpi_read_memcpy_reduce(line, &data_memcpy_reduce) >= 0) {
	    if (data_memcpy_reduce.buffer_type1 == erecvbufp) partner_recv[data_memcpy_reduce.buffer_number1] = 1;
	    if (data_memcpy_reduce.buffer_type2 == erecvbufp) partner_recv[data_memcpy_reduce.buffer_number2] = 1;
	  }
	}
      }
    }
  } while (flag3);
  ext_mpi_delete_parameters(parameters);
  nbuffer_in = 0;
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  free(parameters->mem_partners_send);
  parameters->mem_partners_send_max = 0;
  parameters->mem_partners_send = (int*)malloc(sizeof(int) * parameters->socket_row_size * parameters->num_sockets_per_node);
  for (i = 0; i < parameters->socket_row_size * parameters->num_sockets_per_node; i++) {
    if (partner_send[i]) parameters->mem_partners_send[parameters->mem_partners_send_max++] = i;
  }
  if (!parameters->mem_partners_send_max) {
    free(parameters->mem_partners_send);
    parameters->mem_partners_send = NULL;
  }
  free(parameters->mem_partners_recv);
  parameters->mem_partners_recv_max = 0;
  parameters->mem_partners_recv = (int*)malloc(sizeof(int) * parameters->socket_row_size * parameters->num_sockets_per_node);
  for (i = 0; i < parameters->socket_row_size * parameters->num_sockets_per_node; i++) {
    if (partner_recv[i]) parameters->mem_partners_recv[parameters->mem_partners_recv_max++] = i;
  }
  if (!parameters->mem_partners_recv_max) {
    free(parameters->mem_partners_recv);
    parameters->mem_partners_recv = NULL;
  }
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag3 =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag3) {
      nbuffer_out +=
          ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
    }
  } while (flag3);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  free(partner_recv);
  free(partner_send);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
}
