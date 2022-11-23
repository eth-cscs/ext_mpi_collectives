#include "raw_code_tasks_node.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_raw_code_tasks_node(char *buffer_in, char *buffer_out) {
  int *nodes_recv = NULL, *nodes_send = NULL, node_rank, node_row_size = 1,
      node_column_size = 1, node_size;
  int node, num_nodes;
  int nbuffer_out = 0, nbuffer_in = 0, i, j, k, l;
  struct data_algorithm data, data_org;
  struct parameters_block *parameters;
  int *rank_perm = NULL, *rank_back_perm = NULL, msizes_max = -1;
  data.num_blocks = data_org.num_blocks = 0;
  data.blocks = data_org.blocks = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  node = parameters->socket;
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
  i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data_org, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm raw_code_tasks_node\n");
    exit(2);
  }
  i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm raw_code_tasks_node\n");
    exit(2);
  }
  for (i = 0; i < data.num_blocks; i++) {
    for (j = 0; j < num_nodes; j++) {
      nodes_recv[j] = nodes_send[j] = -1;
    }
    for (j = 0; j < data.blocks[i].num_lines; j++) {
      for (k = 0; k < data.blocks[i].lines[j].recvfrom_max; k++) {
        if (data.blocks[i].lines[j].recvfrom_node[k] <= -10) {
          data.blocks[i].lines[j].recvfrom_node[k] = -10 - data.blocks[i].lines[j].recvfrom_node[k];
        }
      }
      for (k = 0; k < data.blocks[i].lines[j].sendto_max; k++) {
        if (data.blocks[i].lines[j].sendto_node[k] <= -10) {
          data.blocks[i].lines[j].sendto_node[k] = -10 - data.blocks[i].lines[j].sendto_node[k];
        }
      }
    }
    for (j = 0; j < data.blocks[i].num_lines; j++) {
      for (k = 0; k < data.blocks[i].lines[j].recvfrom_max; k++) {
        if (data.blocks[i].lines[j].recvfrom_node[k] >= 0) {
          nodes_recv[(+rank_perm[data.blocks[i].lines[j].recvfrom_node[k]] - rank_perm[node] +
                      num_nodes) %
                     num_nodes] = (+rank_perm[data.blocks[i].lines[j].recvfrom_node[k]] -
                                   rank_perm[node] + num_nodes) %
                                  num_nodes;
        }
        if (data.blocks[i].lines[j].recvfrom_node[k] <= -10) {
          nodes_recv[(+rank_perm[10 - data.blocks[i].lines[j].recvfrom_node[k]] -
                      rank_perm[node] + num_nodes) %
                     num_nodes] = (+rank_perm[10 - data.blocks[i].lines[j].recvfrom_node[k]] -
                                   rank_perm[node] + num_nodes) %
                                  num_nodes;
        }
      }
      for (k = 0; k < data.blocks[i].lines[j].sendto_max; k++) {
        if (data.blocks[i].lines[j].sendto_node[k] >= 0) {
          nodes_send[(-rank_perm[data.blocks[i].lines[j].sendto_node[k]] + rank_perm[node] +
                      num_nodes) %
                     num_nodes] =
              (-rank_perm[data.blocks[i].lines[j].sendto_node[k]] + rank_perm[node] + num_nodes) %
              num_nodes;
        }
      }
      for (k = 0; k < data.blocks[i].lines[j].sendto_max; k++) {
        if (data.blocks[i].lines[j].sendto_node[k] <= -10) {
          nodes_send[(-rank_perm[10 - data.blocks[i].lines[j].sendto_node[k]] + rank_perm[node] +
                      num_nodes) %
                     num_nodes] = (-rank_perm[10 - data.blocks[i].lines[j].sendto_node[k]] +
                                   rank_perm[node] + num_nodes) %
                                  num_nodes;
        }
      }
    }
    k = 0;
    nodes_recv[0] = nodes_recv[0] * node_size;
    for (j = 1; j < num_nodes; j++) {
      if (nodes_recv[j] >= 0) {
        if (k % node_size != node_rank) {
          nodes_recv[j] = -nodes_recv[j] * node_size - 10;
        } else {
          nodes_recv[j] = nodes_recv[j] * node_size;
        }
        k++;
      }
    }
    k = 0;
    nodes_send[0] = nodes_send[0] * node_size;
    for (j = 1; j < num_nodes; j++) {
      if (nodes_send[j] >= 0) {
        if (k % node_size != node_rank) {
          nodes_send[j] = -nodes_send[j] * node_size - 10;
        } else {
          nodes_send[j] = nodes_send[j] * node_size;
        }
        k++;
      }
    }
    for (j = 0; j < data.blocks[i].num_lines; j++) {
      for (k = 0; k < data.blocks[i].lines[j].recvfrom_max; k++) {
        if (data.blocks[i].lines[j].recvfrom_node[k] >= 0) {
          l = nodes_recv[(+rank_perm[data.blocks[i].lines[j].recvfrom_node[k]] -
                          rank_perm[node] + num_nodes) %
                         num_nodes];
          if (l >= 0) {
            data.blocks[i].lines[j].recvfrom_node[k] =
                (+l + rank_perm[node] * node_size + num_nodes * node_size) %
                    (num_nodes * node_size) +
                node_rank;
            data.blocks[i].lines[j].recvfrom_node[k] =
                rank_back_perm[data.blocks[i].lines[j].recvfrom_node[k] / node_size] *
                    node_size +
                data.blocks[i].lines[j].recvfrom_node[k] % node_size;
          } else {
            data.blocks[i].lines[j].recvfrom_node[k] =
                -(rank_back_perm[((-l - 10 + rank_perm[node] * node_size +
                                   num_nodes * node_size) %
                                  (num_nodes * node_size)) /
                                 node_size] *
                  node_size) -
                10 - node_rank;
          }
        }
        if (data_org.blocks[i].lines[j].recvfrom_node[k] <= -10) {
          data.blocks[i].lines[j].recvfrom_node[k] = 2000000000;
        }
      }
      for (k = 0; k < data.blocks[i].lines[j].sendto_max; k++) {
        if (data.blocks[i].lines[j].sendto_node[k] >= 0) {
          l = nodes_send[(-rank_perm[data.blocks[i].lines[j].sendto_node[k]] + rank_perm[node] +
                          num_nodes) %
                         num_nodes];
          if (l >= 0) {
            data.blocks[i].lines[j].sendto_node[k] =
                (-l + rank_perm[node] * node_size + num_nodes * node_size) %
                    (num_nodes * node_size) +
                node_rank;
            data.blocks[i].lines[j].sendto_node[k] =
                rank_back_perm[data.blocks[i].lines[j].sendto_node[k] / node_size] * node_size +
                data.blocks[i].lines[j].sendto_node[k] % node_size;
          } else {
            data.blocks[i].lines[j].sendto_node[k] =
                -(rank_back_perm[((+l + 10 + rank_perm[node] * node_size +
                                   num_nodes * node_size) %
                                  (num_nodes * node_size)) /
                                 node_size] *
                  node_size) -
                10 - node_rank;
          }
        }
        if (data_org.blocks[i].lines[j].sendto_node[k] <= -10) {
          data.blocks[i].lines[j].sendto_node[k] = 2000000000;
        }
      }
      for (k = 0; k < data.blocks[i].lines[j].recvfrom_max; k++) {
        if (data.blocks[i].lines[j].recvfrom_node[k] == 2000000000) {
          if (data.blocks[i].lines[j].recvfrom_max == 1) {
            data.blocks[i].lines[j].recvfrom_node[k] = node * node_size + node_rank;
            data.blocks[i].lines[j].recvfrom_line[k] = j;
          } else {
            for (l = k; l < data.blocks[i].lines[j].recvfrom_max - 1; l++) {
              data.blocks[i].lines[j].recvfrom_node[l] = data.blocks[i].lines[j].recvfrom_node[l + 1];
              data.blocks[i].lines[j].recvfrom_line[l] = data.blocks[i].lines[j].recvfrom_line[l + 1];
            }
            data.blocks[i].lines[j].recvfrom_max--;
            k--;
          }
        }
      }
      for (k = 0; k < data.blocks[i].lines[j].sendto_max; k++) {
        if (data.blocks[i].lines[j].sendto_node[k] == 2000000000) {
          if (data.blocks[i].lines[j].sendto_max == 1) {
            data.blocks[i].lines[j].sendto_node[k] = node * node_size + node_rank;
          } else {
            for (l = k; l < data.blocks[i].lines[j].sendto_max - 1; l++) {
              data.blocks[i].lines[j].sendto_node[l] = data.blocks[i].lines[j].sendto_node[l + 1];
            }
            data.blocks[i].lines[j].sendto_max--;
            k--;
          }
        }
      }
    }
  }
  nbuffer_out +=
      ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_algorithm(data_org);
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
  ext_mpi_delete_algorithm(data_org);
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
