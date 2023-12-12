#include "backward_interpreter.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_backward_interpreter(char *buffer_in, char *buffer_out,
                                  MPI_Comm comm_row) {
  int **values = NULL, **recv_values = NULL, max_lines;
  int nbuffer_out = 0, nbuffer_in = 0, partner, i, j, k, l = -1;
  struct data_algorithm data;
  struct parameters_block *parameters;
  MPI_Request *request = NULL;
  data.num_blocks = 0;
  data.blocks = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data, parameters->ascii_in);
  PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm backward_interpreter\n");
    exit(2);
  }
  i = 0;
  values = (int **)malloc(sizeof(int *) * data.num_blocks);
  if (!values)
    i = ERROR_MALLOC;
  PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i < 0)
    goto error;
  for (i = 0; i < data.num_blocks; i++) {
    values[i] = NULL;
  }
  j = 0;
  for (i = 0; i < data.num_blocks; i++) {
    values[i] = (int *)malloc(sizeof(int) * data.blocks[i].num_lines);
    if (!values[i])
      j = ERROR_MALLOC;
  }
  PMPI_Allreduce(MPI_IN_PLACE, &j, 1, MPI_INT, MPI_MIN, comm_row);
  if (j < 0)
    goto error;
  for (i = 0; i < data.num_blocks; i++) {
    for (j = 0; j < data.blocks[i].num_lines; j++) {
      values[i][j] = 0;
    }
  }
  for (i = 0; i < data.blocks[data.num_blocks - 1].num_lines; i++) {
    for (j = 0; j < data.blocks[data.num_blocks - 1].lines[i].sendto_max; j++) {
      if ((data.blocks[data.num_blocks - 1].lines[i].sendto_node[j] == -1) &&
          (parameters->socket == parameters->root)) {
        values[data.num_blocks - 1][i] = 2;
      }
    }
  }
  max_lines = 0;
  for (i = 0; i < data.num_blocks; i++) {
    if (data.blocks[i].num_lines > max_lines) {
      max_lines = data.blocks[i].num_lines;
    }
  }
  i = 0;
  request = (MPI_Request *)malloc(sizeof(MPI_Request) * max_lines * 2);
  if (!request)
    i = ERROR_MALLOC;
  PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i < 0)
    goto error;
  i = 0;
  recv_values = (int **)malloc(sizeof(int *) * max_lines);
  if (!recv_values)
    i = ERROR_MALLOC;
  PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (i < 0)
    goto error;
  for (i = 0; i < max_lines; i++) {
    recv_values[i] = NULL;
  }
  j = 0;
  for (i = 0; i < max_lines; i++) {
    recv_values[i] = (int *)malloc(sizeof(int) * parameters->num_sockets * parameters->socket_row_size);
    if (!recv_values[i])
      j = ERROR_MALLOC;
  }
  PMPI_Allreduce(MPI_IN_PLACE, &j, 1, MPI_INT, MPI_MIN, comm_row);
  if (j < 0)
    goto error;
  for (i = data.num_blocks - 1; i >= 0; i--) {
    if (i < data.num_blocks - 1) {
      for (j = data.blocks[i].num_lines - 1; j >= 0; j--) {
        if (j < data.blocks[i + 1].num_lines) {
          if (values[i + 1][j]) {
            values[i][j] = 1;
          }
        }
      }
    }
    for (j = data.blocks[i].num_lines - 1; j >= 0; j--) {
      for (k = 0; k < data.blocks[i].lines[j].reducefrom_max; k++) {
	if (values[i][data.blocks[i].lines[j].reducefrom[k]]) {
//	  values[i][j] = 1;
        }
      }
      if (values[i][j]) {
        for (k = 0; k < data.blocks[i].lines[j].reducefrom_max; k++) {
	  if (!values[i][data.blocks[i].lines[j].reducefrom[k]]) {
	    values[i][data.blocks[i].lines[j].reducefrom[k]] = 1;
	  }
        }
        for (k = 0; k < data.blocks[i].lines[j].copyreducefrom_max; k++) {
	  if (!values[i][data.blocks[i].lines[j].copyreducefrom[k]]) {
	    values[i][data.blocks[i].lines[j].copyreducefrom[k]] = 1;
	  }
	  if (i > 0) values[i - 1][j] = 0;
        }
      }
    }
    if (i < data.num_blocks - 1 && i > 0) {
      l = 0;
      for (j = data.blocks[i].num_lines - 1; j >= 0; j--) {
        for (k = 0; k < data.blocks[i].lines[j].sendto_max; k++) {
          if (data.blocks[i].lines[j].sendto_node[k] != parameters->socket) {
	    partner = data.blocks[i].lines[j].sendto_node[k];
            MPI_Irecv(&recv_values[j][partner], 1, MPI_INT, partner * parameters->socket_row_size + parameters->socket_rank, 0, comm_row, &request[l++]);
          }
        }
      }
      for (j = data.blocks[i].num_lines - 1; j >= 0; j--) {
        for (k = 0; k < data.blocks[i].lines[j].recvfrom_max; k++) {
	  partner = data.blocks[i].lines[j].recvfrom_node[k];
          if (partner == parameters->socket) {
	    printf("logical error 1 backward_interpreter\n");
	    exit(1);
          } else {
            MPI_Isend(&values[i][j], 1, MPI_INT, partner * parameters->socket_row_size + parameters->socket_rank, 0, comm_row, &request[l++]);
          }
        }
      }
      PMPI_Waitall(l, request, MPI_STATUSES_IGNORE);
    }
    if (i < data.num_blocks - 1 && i > 0) {
      for (j = 0; j < data.blocks[i].num_lines; j++) {
        for (k = 0; k < data.blocks[i].lines[j].sendto_max; k++) {
	  partner = data.blocks[i].lines[j].sendto_node[k];
          if (partner != parameters->socket && partner >= 0) {
            if (recv_values[j][partner]) {
              values[i][j] = 2;
            } else {
	      data.blocks[i].lines[j].sendto_node[k] = -10 - data.blocks[i].lines[j].sendto_node[k];
/*              data.blocks[i].lines[j].sendto_max--;
	      for (l = k; l < data.blocks[i].lines[j].sendto_max; l++) {
	        data.blocks[i].lines[j].sendto_node[l] = data.blocks[i].lines[j].sendto_node[l + 1];
	      }
	      k--;*/
            }
          } else {
	    printf("logical error 2 backward_interpreter\n");
	    exit(1);
	  }
        }
      }
    }
/*    for (j = 0; j < data.blocks[i].num_lines; j++) {
      for (k = 0; k < data.blocks[i].lines[j].reducefrom_max; k++) {
	if (values[i][data.blocks[i].lines[j].reducefrom[k]]) {
//	  values[i][j] = 1;
        }
      }
      if (values[i][j]) {
        for (k = 0; k < data.blocks[i].lines[j].reducefrom_max; k++) {
	  if (!values[i][data.blocks[i].lines[j].reducefrom[k]]) {
	    values[i][data.blocks[i].lines[j].reducefrom[k]] = 1;
	  }
        }
        for (k = 0; k < data.blocks[i].lines[j].copyreducefrom_max; k++) {
	  if (!values[i][data.blocks[i].lines[j].copyreducefrom[k]]) {
	    values[i][data.blocks[i].lines[j].copyreducefrom[k]] = 1;
	  }
	  if (i > 0) values[i - 1][j] = 0;
        }
      }
    }*/
    for (j = 0; j < data.blocks[i].num_lines; j++) {
/*      if (values[i][j] < 2) {
        if (data.blocks[i].lines[j].sendto_max > 0) {
          free(data.blocks[i].lines[j].sendto_node);
          free(data.blocks[i].lines[j].sendto_line);
          data.blocks[i].lines[j].sendto_node = data.blocks[i].lines[j].sendto_line = NULL;
          data.blocks[i].lines[j].sendto_max = 0;
        }
      }*/
      if (!values[i][j]) {
        if (data.blocks[i].lines[j].recvfrom_max > 0) {
	  for (k = 0; k < data.blocks[i].lines[j].recvfrom_max; k++) {
	    data.blocks[i].lines[j].recvfrom_node[k] = -10 - data.blocks[i].lines[j].recvfrom_node[k];
	  }
/*          free(data.blocks[i].lines[j].recvfrom_node);
          free(data.blocks[i].lines[j].recvfrom_line);
          data.blocks[i].lines[j].recvfrom_node = NULL;
          data.blocks[i].lines[j].recvfrom_line = NULL;
          data.blocks[i].lines[j].recvfrom_max = 0;*/
        }
        if (data.blocks[i].lines[j].reducefrom_max > 0) {
          free(data.blocks[i].lines[j].reducefrom);
          data.blocks[i].lines[j].reducefrom = NULL;
	  data.blocks[i].lines[j].reducefrom_max = 0;
        }
        if (data.blocks[i].lines[j].copyreducefrom_max > 0) {
          free(data.blocks[i].lines[j].copyreducefrom);
          data.blocks[i].lines[j].copyreducefrom = NULL;
	  data.blocks[i].lines[j].copyreducefrom_max = 0;
        }
      }
    }
  }
  nbuffer_out +=
      ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  free(request);
  for (i = max_lines - 1; i >= 0; i--) {
    free(recv_values[i]);
  }
  free(recv_values);
  for (i = data.num_blocks - 1; i >= 0; i--) {
    free(values[i]);
  }
  free(values);
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  free(request);
  if (recv_values) {
    for (i = max_lines - 1; i >= 0; i--) {
      free(recv_values[i]);
    }
  }
  free(recv_values);
  if (values) {
    for (i = data.num_blocks - 1; i >= 0; i--) {
      free(values[i]);
    }
  }
  free(values);
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
