#include "raw_code_tasks_node_master.h"
#include "constants.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_raw_code_tasks_node_master(char *buffer_in, char *buffer_out) {
  int *nodes_recv = NULL, *nodes_send = NULL, node_rank, node_row_size = 1,
      node_column_size = 1, node_size;
  int node, num_nodes;
  int nbuffer_out = 0, nbuffer_in = 0, i, j, k, size_level0 = 0,
      *size_level1 = NULL;
  struct data_line **data = NULL;
  struct parameters_block *parameters;
  int *rank_perm = NULL, *rank_back_perm = NULL, msizes_max = -1;
  nbuffer_in += i = read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  node = parameters->node;
  num_nodes = parameters->num_nodes;
  node_rank = parameters->node_rank;
  node_row_size = parameters->node_row_size;
  node_column_size = parameters->node_column_size;
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
  nbuffer_in += i = read_algorithm(buffer_in + nbuffer_in, &size_level0,
                                   &size_level1, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm raw_code_tasks_node_master\n");
    exit(2);
  }
  for (i = 0; i < size_level0; i++) {
    for (j = 0; j < size_level1[i]; j++) {
      for (k = 0; k < data[i][j].to_max; k++) {
        if (data[i][j].to[k] != -1) {
          if (node_rank == 0 || node == data[i][j].to[k]) {
            data[i][j].to[k] = data[i][j].to[k] * node_size + node_rank;
          } else {
            data[i][j].to[k] = -10 - (data[i][j].to[k] * node_size + node_rank);
          }
        }
      }
      for (k = 0; k < data[i][j].from_max; k++) {
        if (data[i][j].from_node[k] != -1) {
          if (node_rank == 0 || node == data[i][j].from_node[k]) {
            data[i][j].from_node[k] =
                data[i][j].from_node[k] * node_size + node_rank;
          } else {
            data[i][j].from_node[k] =
                -10 - (data[i][j].from_node[k] * node_size + node_rank);
          }
        }
      }
      nbuffer_out += sprintf(buffer_out + nbuffer_out, "\n");
    }
    nbuffer_out += sprintf(buffer_out + nbuffer_out, "#\n");
  }
  nbuffer_out +=
      write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                      parameters->ascii_out);
  nbuffer_out += write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  delete_algorithm(size_level0, size_level1, data);
  free(rank_back_perm);
  free(rank_perm);
  parameters->rank_perm = NULL;
  parameters->rank_perm_max = 0;
  delete_parameters(parameters);
  free(nodes_send);
  free(nodes_recv);
  return nbuffer_out;
error:
  delete_algorithm(size_level0, size_level1, data);
  free(rank_back_perm);
  free(rank_perm);
  if (parameters) {
    parameters->rank_perm = NULL;
    parameters->rank_perm_max = 0;
  }
  delete_parameters(parameters);
  free(nodes_send);
  free(nodes_recv);
  return ERROR_MALLOC;
}
