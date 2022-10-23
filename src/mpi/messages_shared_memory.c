#include "messages_shared_memory.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct line_irecv_isend_node {
  struct line_irecv_isend data;
  int socket;
};

struct line_irecv_isend_list {
  struct line_irecv_isend_node data_irecv_isend, data_irecv_isend_ref;
  MPI_Request request;
  int socket;
  struct line_irecv_isend_list *next;
};

int ext_mpi_messages_shared_memory(char *buffer_in, char *buffer_out, MPI_Comm comm_row,
                               int node_num_cores_row, MPI_Comm comm_column,
                               int node_num_cores_column) {
  struct line_memcpy_reduce data_memcpy_reduce;
  struct line_irecv_isend_node data_irecv_isend;
  struct line_irecv_isend_list *data_irecv_isend_list_recv = NULL, *data_irecv_isend_list_send = NULL, *data_irecv_isend_list_temp;
  char line[1000];
  enum eassembler_type estring1;
  int nbuffer_out = 0, integer1, ascii, socket, socket_rank, node_sockets;
  struct parameters_block *parameters;
  buffer_in += ext_mpi_read_parameters(buffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  ascii = parameters->ascii_in;
  socket = parameters->socket;
  socket_rank = parameters->socket_rank;
  node_sockets = parameters->node_sockets;
  ext_mpi_delete_parameters(parameters);
  while ((integer1 = ext_mpi_read_line(buffer_in, line, ascii)) > 0) {
    buffer_in += integer1;
    ext_mpi_read_assembler_line(line, 0, "sd", &estring1, &integer1);
    if (estring1 == ewaitall) {
      if (socket_rank == 0) {
        data_memcpy_reduce.type = ememcpy;
      } else {
        data_memcpy_reduce.type = ememcp_;
      }
      data_irecv_isend_list_temp = data_irecv_isend_list_recv;
      while (data_irecv_isend_list_temp) {
        MPI_Wait(&data_irecv_isend_list_temp->request, MPI_STATUS_IGNORE);
        data_memcpy_reduce.buffer_type1 = data_irecv_isend_list_temp->data_irecv_isend_ref.data.buffer_type;
        data_memcpy_reduce.buffer_number1 = data_irecv_isend_list_temp->data_irecv_isend_ref.data.buffer_number;
        data_memcpy_reduce.is_offset1 = data_irecv_isend_list_temp->data_irecv_isend_ref.data.is_offset;
        data_memcpy_reduce.offset_number1 = data_irecv_isend_list_temp->data_irecv_isend_ref.data.offset_number;
        data_memcpy_reduce.offset1= data_irecv_isend_list_temp->data_irecv_isend_ref.data.offset;
        data_memcpy_reduce.buffer_type2 = data_irecv_isend_list_temp->data_irecv_isend.data.buffer_type;
        data_memcpy_reduce.buffer_number2 = data_irecv_isend_list_temp->data_irecv_isend.data.buffer_number;
        data_memcpy_reduce.is_offset2 = data_irecv_isend_list_temp->data_irecv_isend.data.is_offset;
        data_memcpy_reduce.offset_number2 = data_irecv_isend_list_temp->data_irecv_isend.data.offset_number;
        data_memcpy_reduce.offset2 = data_irecv_isend_list_temp->data_irecv_isend.data.offset;
        data_memcpy_reduce.size = data_irecv_isend_list_temp->data_irecv_isend.data.size;
        if (data_irecv_isend_list_temp->socket/node_sockets == socket/node_sockets) {
          data_memcpy_reduce.buffer_number2 = (socket + data_irecv_isend_list_temp->socket) % node_sockets;
          nbuffer_out += ext_mpi_write_memcpy_reduce(buffer_out + nbuffer_out, &data_memcpy_reduce, parameters->ascii_out);
        }
      }
      data_irecv_isend_list_temp = data_irecv_isend_list_send;
      while (data_irecv_isend_list_temp) {
        MPI_Wait(&data_irecv_isend_list_temp->request, MPI_STATUS_IGNORE);
      }
    }
    if ((estring1 == eisend) || (estring1 == eirecv)) {
      ext_mpi_read_irecv_isend(line, &data_irecv_isend.data);
      if (estring1 == eisend) {
        data_irecv_isend_list_temp = (struct line_irecv_isend_list*)malloc(sizeof(*data_irecv_isend_list_temp));
        data_irecv_isend_list_temp->socket = socket;
        data_irecv_isend_list_temp->next = data_irecv_isend_list_send;
        data_irecv_isend_list_send = data_irecv_isend_list_temp;
        data_irecv_isend_list_send->data_irecv_isend = data_irecv_isend;
        MPI_Isend(&data_irecv_isend_list_send->data_irecv_isend, sizeof(data_irecv_isend_list_send->data_irecv_isend), MPI_CHAR, data_irecv_isend.data.partner, 0, MPI_COMM_WORLD, &data_irecv_isend_list_send->request);
      } else {
        data_irecv_isend_list_temp = (struct line_irecv_isend_list*)malloc(sizeof(*data_irecv_isend_list_temp));
        data_irecv_isend_list_temp->socket = -1;
        data_irecv_isend_list_temp->next = data_irecv_isend_list_recv;
        data_irecv_isend_list_recv = data_irecv_isend_list_temp;
        data_irecv_isend_list_send->data_irecv_isend_ref = data_irecv_isend;
        MPI_Irecv(&data_irecv_isend_list_recv->data_irecv_isend, sizeof(data_irecv_isend_list_recv->data_irecv_isend), MPI_CHAR, data_irecv_isend.data.partner, 0, MPI_COMM_WORLD, &data_irecv_isend_list_recv->request);
      }
    }
    nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
  }
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  return nbuffer_out;
}
