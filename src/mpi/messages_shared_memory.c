#include "messages_shared_memory.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct line_irecv_isend_node {
  struct line_irecv_isend data;
  int socket;
};

struct line_irecv_isend_list {
  struct line_irecv_isend_node data_irecv_isend, data_irecv_isend_reversed;
  MPI_Request request, request_reversed;
  struct line_irecv_isend_list *next;
};

struct line_list {
  char line[1000];
  struct line_list *next;
};

static int flush_lines(char *buffer_out, struct line_list **lines_listed_org, int all, int ascii_out) {
  struct line_list *lines_listed, *p1;
  int nbuffer_out = 0;
  lines_listed = *lines_listed_org;
  while (lines_listed) { 
    if (lines_listed->next || all) {
      nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, lines_listed->line, ascii_out);
    }
    p1 = lines_listed;
    lines_listed = lines_listed->next;
    free(p1);
  }
  *lines_listed_org = NULL;
  return nbuffer_out;
}

static int delete_element(struct line_list **lines_listed_org, struct line_list **lines_listed_p){
  struct line_list *lines_listed_p2;
  lines_listed_p2 = *lines_listed_org;
  if (lines_listed_p2 == *lines_listed_p) {
    *lines_listed_org = *lines_listed_p = (*lines_listed_p)->next;
    free(lines_listed_p2);
  } else {
    if (!lines_listed_p2->next) return 0;
    while (lines_listed_p2->next != *lines_listed_p) {
      lines_listed_p2 = lines_listed_p2->next;
      if (!lines_listed_p2->next) return 0;
    }
    lines_listed_p2->next = lines_listed_p2->next->next;
    free(*lines_listed_p);
    *lines_listed_p = *lines_listed_org;
  }
  return 1;
}

static int delete_from_list(struct line_list **lines_listed_org, int socket, int num_sockets_per_node, int socket_tasks){
  struct line_irecv_isend_node data_irecv_isend;
  struct line_list *lines_listed_p1;
  enum eassembler_type estring1;
  int integer1, flag, num_comm, ret = 0;
  lines_listed_p1 = *lines_listed_org;
  if (lines_listed_p1) {
    while (lines_listed_p1) {
      ext_mpi_read_assembler_line(lines_listed_p1->line, 0, "sd", &estring1, &integer1);
      flag = 1;
      if (estring1 == ewaitall) {
        num_comm = integer1 - ret;
//        if (num_comm) {
          ext_mpi_write_assembler_line(lines_listed_p1->line, 0, "sdsd", ewaitall, num_comm, elocmemp, 0);
//        } else {
//          delete_element(lines_listed_org, &lines_listed_p1);
//        }
      }
      if ((estring1 == eisend) || (estring1 == eirecv) || (estring1 == eisen_) || (estring1 == eirec_)) {
        ext_mpi_read_irecv_isend(lines_listed_p1->line, &data_irecv_isend.data);
        if (data_irecv_isend.data.partner/num_sockets_per_node/socket_tasks == socket/num_sockets_per_node) {
          delete_element(lines_listed_org, &lines_listed_p1);
          if ((estring1 == eisend) || (estring1 == eirecv)) {
            ret++;
          }
          flag = 0;
        }
      }
      if (flag) {
        lines_listed_p1 = lines_listed_p1->next;
      }
    }
  }
  return ret;
}

static struct line_list* new_last_element(struct line_list **lines_listed_org){
  struct line_list *p;
  if (*lines_listed_org == NULL){
    *lines_listed_org = (struct line_list*)malloc(sizeof(struct line_list));
    (*lines_listed_org)->next = 0;
    return *lines_listed_org;
  }
  p = *lines_listed_org;
  while (p->next){
    p = p->next;
  }
  p->next = (struct line_list*)malloc(sizeof(struct line_list));
  p->next->next = NULL;
  return p->next;
}

int ext_mpi_messages_shared_memory(char *buffer_in, char *buffer_out, MPI_Comm comm_row,
                               int node_num_cores_row, MPI_Comm comm_column,
                               int node_num_cores_column) {
  struct line_memcpy_reduce data_memcpy_reduce;
  struct line_irecv_isend_node data_irecv_isend;
  struct line_irecv_isend_list *data_irecv_isend_list_recv = NULL, *data_irecv_isend_list_send = NULL, *data_irecv_isend_list_temp;
  struct line_list *lines_listed, *lines_listed_org;
  enum eassembler_type estring1;
  int nbuffer_out = 0, integer1, ascii_in, ascii_out, socket, socket_rank, num_sockets_per_node, flag, flag2, flush, socket_tasks;
  struct parameters_block *parameters;
  buffer_in += ext_mpi_read_parameters(buffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  ascii_in = parameters->ascii_in;
  ascii_out = parameters->ascii_out;
  socket = parameters->socket;
  socket_rank = parameters->socket_rank;
  num_sockets_per_node = parameters->num_sockets_per_node;
  socket_tasks = parameters->socket_row_size * parameters->socket_column_size;
  ext_mpi_delete_parameters(parameters);
  lines_listed_org = lines_listed = (struct line_list*)malloc(sizeof(struct line_list));
  lines_listed->next = NULL;
  while ((integer1 = ext_mpi_read_line(buffer_in, lines_listed->line, ascii_in)) > 0) {
    flush = 0;
    buffer_in += integer1;
    ext_mpi_read_assembler_line(lines_listed->line, 0, "sd", &estring1, &integer1);
    if (estring1 == ewaitall) {
      if (socket_rank == 0) {
        data_memcpy_reduce.type = ememcpy;
      } else {
        data_memcpy_reduce.type = ememcp_;
      }
      flag = 0;
      while (data_irecv_isend_list_recv) {
        MPI_Wait(&data_irecv_isend_list_recv->request, MPI_STATUS_IGNORE);
        MPI_Wait(&data_irecv_isend_list_recv->request_reversed, MPI_STATUS_IGNORE);
        data_memcpy_reduce.buffer_type1 = data_irecv_isend_list_recv->data_irecv_isend_reversed.data.buffer_type;
        data_memcpy_reduce.buffer_number1 = data_irecv_isend_list_recv->data_irecv_isend_reversed.data.buffer_number;
        data_memcpy_reduce.is_offset1 = data_irecv_isend_list_recv->data_irecv_isend_reversed.data.is_offset;
        data_memcpy_reduce.offset_number1 = data_irecv_isend_list_recv->data_irecv_isend_reversed.data.offset_number;
        data_memcpy_reduce.offset1= data_irecv_isend_list_recv->data_irecv_isend_reversed.data.offset;
        data_memcpy_reduce.buffer_type2 = data_irecv_isend_list_recv->data_irecv_isend.data.buffer_type;
        data_memcpy_reduce.buffer_number2 = data_irecv_isend_list_recv->data_irecv_isend.data.buffer_number;
        data_memcpy_reduce.is_offset2 = data_irecv_isend_list_recv->data_irecv_isend.data.is_offset;
        data_memcpy_reduce.offset_number2 = data_irecv_isend_list_recv->data_irecv_isend.data.offset_number;
        data_memcpy_reduce.offset2 = data_irecv_isend_list_recv->data_irecv_isend.data.offset;
        data_memcpy_reduce.size = data_irecv_isend_list_recv->data_irecv_isend.data.size;
        if (data_irecv_isend_list_recv->data_irecv_isend.socket/num_sockets_per_node == socket/num_sockets_per_node) {
          data_memcpy_reduce.buffer_number2 = (num_sockets_per_node - data_irecv_isend_list_recv->data_irecv_isend.socket + socket) % num_sockets_per_node;
          if (!flag) {
            ext_mpi_write_assembler_line(new_last_element(&lines_listed_org)->line, 0, "s", esocket_barrier);
            ext_mpi_write_assembler_line(new_last_element(&lines_listed_org)->line, 0, "s", enode_barrier);
            flag = 1;
          }
          ext_mpi_write_memcpy_reduce(new_last_element(&lines_listed_org)->line, &data_memcpy_reduce, 0);
          delete_from_list(&lines_listed_org, socket, num_sockets_per_node, socket_tasks);
        }
        data_irecv_isend_list_temp = data_irecv_isend_list_recv;
        data_irecv_isend_list_recv = data_irecv_isend_list_recv->next;
        free(data_irecv_isend_list_temp);
      }
      data_irecv_isend_list_recv = NULL;
      PMPI_Allreduce(&flag, &flag2, 1, MPI_INT, MPI_MAX, comm_row);
      if (flag2) {
        if (!flag) {
          ext_mpi_write_assembler_line(new_last_element(&lines_listed_org)->line, 0, "s", enode_barrier);
          ext_mpi_write_assembler_line(new_last_element(&lines_listed_org)->line, 0, "s", esocket_barrier);
        }
        ext_mpi_write_assembler_line(new_last_element(&lines_listed_org)->line, 0, "s", enode_barrier);
        ext_mpi_write_assembler_line(new_last_element(&lines_listed_org)->line, 0, "s", esocket_barrier);
      }
      while (data_irecv_isend_list_send) {
        MPI_Wait(&data_irecv_isend_list_send->request, MPI_STATUS_IGNORE);
        MPI_Wait(&data_irecv_isend_list_send->request_reversed, MPI_STATUS_IGNORE);
        if (data_irecv_isend_list_send->data_irecv_isend_reversed.socket/num_sockets_per_node == socket/num_sockets_per_node) {
          delete_from_list(&lines_listed_org, socket, num_sockets_per_node, socket_tasks);
        }
        data_irecv_isend_list_temp = data_irecv_isend_list_send;
        data_irecv_isend_list_send = data_irecv_isend_list_send->next;
        free(data_irecv_isend_list_temp);
      }
      data_irecv_isend_list_send = NULL;
      nbuffer_out += flush_lines(buffer_out + nbuffer_out, &lines_listed_org, 1, ascii_out);
      lines_listed_org = lines_listed = (struct line_list*)malloc(sizeof(struct line_list));
      lines_listed->next = NULL;
      flush = 1;
    }
    if ((estring1 == eisend) || (estring1 == eirecv) || (estring1 == eisen_) || (estring1 == eirec_)) {
      ext_mpi_read_irecv_isend(lines_listed->line, &data_irecv_isend.data);
      if ((estring1 == eisend) || (estring1 == eisen_)) {
        data_irecv_isend_list_temp = (struct line_irecv_isend_list*)malloc(sizeof(struct line_irecv_isend_list));
        data_irecv_isend_list_temp->next = data_irecv_isend_list_send;
        data_irecv_isend_list_send = data_irecv_isend_list_temp;
        data_irecv_isend_list_send->data_irecv_isend = data_irecv_isend;
        data_irecv_isend_list_temp->data_irecv_isend.socket = socket;
        MPI_Isend(&data_irecv_isend_list_send->data_irecv_isend, sizeof(struct line_irecv_isend_node), MPI_CHAR, data_irecv_isend.data.partner, 0, comm_row, &data_irecv_isend_list_send->request);
        MPI_Irecv(&data_irecv_isend_list_send->data_irecv_isend_reversed.socket, sizeof(int), MPI_CHAR, data_irecv_isend.data.partner, 1, comm_row, &data_irecv_isend_list_send->request_reversed);
      } else {
        data_irecv_isend_list_temp = (struct line_irecv_isend_list*)malloc(sizeof(struct line_irecv_isend_list));
        data_irecv_isend_list_temp->next = data_irecv_isend_list_recv;
        data_irecv_isend_list_recv = data_irecv_isend_list_temp;
        data_irecv_isend_list_recv->data_irecv_isend_reversed = data_irecv_isend;
        data_irecv_isend_list_temp->data_irecv_isend_reversed.socket = socket;
        MPI_Irecv(&data_irecv_isend_list_recv->data_irecv_isend, sizeof(struct line_irecv_isend_node), MPI_CHAR, data_irecv_isend.data.partner, 0, comm_row, &data_irecv_isend_list_recv->request);
        MPI_Isend(&data_irecv_isend_list_recv->data_irecv_isend_reversed.socket, sizeof(int), MPI_CHAR, data_irecv_isend.data.partner, 1, comm_row, &data_irecv_isend_list_recv->request_reversed);
      }
    }
    if (!flush) {
      lines_listed->next = (struct line_list*)malloc(sizeof(struct line_list));
      lines_listed->next->next = NULL;
      lines_listed = lines_listed->next;
    }
  }
  nbuffer_out += flush_lines(buffer_out + nbuffer_out, &lines_listed_org, 0, ascii_out);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, ascii_out);
  return nbuffer_out;
}
