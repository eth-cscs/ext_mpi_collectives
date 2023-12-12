#include "raw_code_tasks_node_master.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_raw_code_tasks_node_master(char *buffer_in, char *buffer_out) {
  int *nodes_recv = NULL, *nodes_send = NULL, node_rank, node_row_size = 1,
      node_column_size = 1, node_size;
  int num_nodes;
  int nbuffer_out = 0, nbuffer_in = 0, i, j, k;
  struct data_algorithm data;
  struct parameters_block *parameters;
  int *rank_perm = NULL, *rank_back_perm = NULL, msizes_max = -1;
  memset(&data, 0, sizeof(struct data_algorithm));
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  num_nodes = parameters->num_sockets;
  node_rank = parameters->socket_rank;
  node_row_size = parameters->socket_row_size;
  node_column_size = parameters->socket_column_size;
  msizes_max = parameters->rank_perm_max;
  rank_perm = parameters->rank_perm;
  if (!rank_perm) {
    msizes_max = num_nodes;
    rank_perm = (int *)malloc(msizes_max * sizeof(int));
    if (!rank_perm)
      goto error;
    for (i = 0; i < msizes_max; i++) {
      rank_perm[i] = i;
    }
  }
  rank_back_perm = (int *)malloc(msizes_max * sizeof(int));
  if (!rank_back_perm)
    goto error;
  for (i = 0; i < msizes_max; i++) {
    rank_back_perm[rank_perm[i]] = i;
  }
  node_size = node_row_size * node_column_size;
  nodes_recv = (int *)malloc(sizeof(int) * num_nodes);
  if (!nodes_recv)
    goto error;
  nodes_send = (int *)malloc(sizeof(int) * num_nodes);
  if (!nodes_send)
    goto error;
  nbuffer_in += i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm raw_code_tasks_node_master\n");
    exit(2);
  }
  for (i = 0; i < data.num_blocks; i++) {
    for (j = 0; j < data.blocks[i].num_lines; j++) {
      for (k = 0; k < data.blocks[i].lines[j].sendto_max; k++) {
        if (data.blocks[i].lines[j].sendto_node[k] != -1) {
          if (node_rank == 0) {
	    if (data.blocks[i].lines[j].sendto_node[k] >= 0) {
              data.blocks[i].lines[j].sendto_node[k] = data.blocks[i].lines[j].sendto_node[k] * node_size + node_rank;
	    } else {
              data.blocks[i].lines[j].sendto_node[k] = -10 - ((-10 - data.blocks[i].lines[j].sendto_node[k]) * node_size + node_rank);
	    }
          } else {
	    if (data.blocks[i].lines[j].sendto_node[k] >= 0) {
              data.blocks[i].lines[j].sendto_node[k] = -10 - (data.blocks[i].lines[j].sendto_node[k] * node_size + node_rank);
	    } else {
              data.blocks[i].lines[j].sendto_node[k] = -10 - ((-10 - data.blocks[i].lines[j].sendto_node[k]) * node_size + node_rank);
	    }
          }
        }
      }
      for (k = 0; k < data.blocks[i].lines[j].recvfrom_max; k++) {
        if (data.blocks[i].lines[j].recvfrom_node[k] != -1) {
          if (node_rank == 0) {
	    if (data.blocks[i].lines[j].recvfrom_node[k] >= 0) {
              data.blocks[i].lines[j].recvfrom_node[k] = data.blocks[i].lines[j].recvfrom_node[k] * node_size + node_rank;
	    } else {
              data.blocks[i].lines[j].recvfrom_node[k] = -10 - ((-10 - data.blocks[i].lines[j].recvfrom_node[k]) * node_size + node_rank);
	    }
          } else {
	    if (data.blocks[i].lines[j].recvfrom_node[k] >= 0) {
              data.blocks[i].lines[j].recvfrom_node[k] = -10 - (data.blocks[i].lines[j].recvfrom_node[k] * node_size + node_rank);
	    } else {
              data.blocks[i].lines[j].recvfrom_node[k] = -10 - ((-10 - data.blocks[i].lines[j].recvfrom_node[k]) * node_size + node_rank);
	    }
          }
        }
      }
    }
  }
  nbuffer_out += ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(data);
  free(rank_back_perm);
  free(rank_perm);
  parameters->rank_perm = NULL;
  parameters->rank_perm_max = 0;
  ext_mpi_delete_parameters(parameters);
  free(nodes_send);
  free(nodes_recv);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(data);
  free(rank_back_perm);
  free(rank_perm);
  if (parameters) {
    parameters->rank_perm = NULL;
    parameters->rank_perm_max = 0;
  }
  ext_mpi_delete_parameters(parameters);
  free(nodes_send);
  free(nodes_recv);
  return ERROR_MALLOC;
}
