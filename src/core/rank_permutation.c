#include "rank_permutation.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rank_perm_heuristic_compare(const void *a, const void *b) {
  return (*(int *)a - *(int *)b);
}

static void rank_perm_heuristic(int num_nodes, int *node_recvcounts, int *rank_perm) {
  typedef struct node_sort {
    int value, key, color;
  } node_t_s;
  typedef struct node {
    int value, key, color;
  } node_t;
  node_t array_org[num_nodes], array_org2[num_nodes];
  node_t_s array[num_nodes];
  int num_colors, flag, i, j, k;
  for (i = 0; i < num_nodes; i++) {
    array_org[i].value = node_recvcounts[i];
    array_org[i].key = array_org[i].color = i;
  }
  flag = 1;
  num_colors = num_nodes;
  while (flag) {
    for (i = 0; i < num_colors; i++) {
      array[i].value = 0;
      array[i].key = i;
    }
    for (i = 0; i < num_nodes; i++) {
      array[array_org[i].color].value += array_org[i].value;
    }
    if (num_colors % 2) {
      array[0].value = 0;
    }
    qsort(array, num_colors, sizeof(node_t_s), rank_perm_heuristic_compare);
    k = 0;
    if (!(num_colors % 2)) {
      for (i = 0; i < num_colors; i++) {
        for (j = 0; j < num_nodes; j++) {
          if (i % 2) {
            if (array[num_colors - 1 - i / 2].key == array_org[j].color) {
              array_org2[k] = array_org[j];
              array_org2[k++].color = i / 2;
            }
          } else {
            if (array[i / 2].key == array_org[j].color) {
              array_org2[k] = array_org[j];
              array_org2[k++].color = i / 2;
            }
          }
        }
      }
    } else {
      for (j = 0; j < num_nodes; j++) {
        if (0 == array_org[j].color) {
          array_org2[k++] = array_org[j];
        }
      }
      for (i = 0; i < num_colors - 1; i++) {
        for (j = 0; j < num_nodes; j++) {
          if (i % 2) {
            if (array[num_colors - 1 - i / 2].key == array_org[j].color) {
              array_org2[k] = array_org[j];
              array_org2[k++].color = i / 2 + 1;
            }
          } else {
            if (array[i / 2 + 1].key == array_org[j].color) {
              array_org2[k] = array_org[j];
              array_org2[k++].color = i / 2 + 1;
            }
          }
        }
      }
    }
    for (i = 0; i < num_nodes; i++) {
      array_org[i] = array_org2[i];
    }
    num_colors = (num_colors + 1) / 2;
    flag = num_colors > 1;
  }
  for (i = 0; i < num_nodes; i++) {
    rank_perm[i] = array_org[i].key;
  }
}

int ext_mpi_generate_rank_permutation_forward(char *buffer_in, char *buffer_out) {
  int num_nodes, flag, msizes_max = 0, *msizes = NULL, *rank_perm = NULL,
                       *rank_back_perm = NULL;
  int nbuffer_out = 0, nbuffer_in = 0, i;
  struct parameters_block *parameters;
  char line[1000];
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  num_nodes = parameters->num_sockets;
  msizes_max = parameters->message_sizes_max;
  msizes = parameters->message_sizes;
  if (num_nodes != msizes_max) {
    printf("wrong number of message sizes\n");
    exit(2);
  }
  rank_perm = (int *)malloc(msizes_max * sizeof(int));
  if (!rank_perm)
    goto error;
  flag = 0;
  for (i = 0; i < msizes_max; i++) {
    if (msizes[i] != msizes[0]) {
      flag = 1;
    }
  }
  if (flag) {
    rank_perm_heuristic(num_nodes, msizes, rank_perm);
  } else {
    for (i = 0; i < msizes_max; i++) {
      rank_perm[i] = i;
    }
  }
  if (parameters->rank_perm_max) {
    free(parameters->rank_perm);
  }
  parameters->rank_perm_max = msizes_max;
  parameters->rank_perm = rank_perm;
  parameters->message_sizes =
      (int *)malloc(sizeof(int) * parameters->message_sizes_max);
  if (!parameters->message_sizes)
    goto error;
  rank_back_perm = (int *)malloc(msizes_max * sizeof(int));
  if (!rank_back_perm)
    goto error;
  for (i = 0; i < msizes_max; i++) {
    rank_back_perm[rank_perm[i]] = i;
  }
  parameters->socket = rank_back_perm[parameters->socket];
  for (i = 0; i < msizes_max; i++) {
    parameters->message_sizes[i] = msizes[rank_back_perm[i]];
  }
  free(msizes);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag > 0) {
      nbuffer_out +=
          ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
    }
  } while (flag);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  free(rank_back_perm);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  free(rank_back_perm);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}

int ext_mpi_generate_rank_permutation_backward(char *buffer_in, char *buffer_out) {
  int num_nodes, flag, msizes_max = 0, *msizes = NULL, *rank_perm = NULL;
  int nbuffer_out = 0, nbuffer_in = 0, i, j, k;
  struct data_algorithm data;
  struct parameters_block *parameters;
  char line[1000];
  data.num_blocks = 0;
  data.blocks = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  num_nodes = parameters->num_sockets;
  msizes_max = parameters->message_sizes_max;
  msizes = parameters->message_sizes;
  rank_perm = parameters->rank_perm;
  if (num_nodes != msizes_max) {
    printf("wrong number of message sizes\n");
    exit(2);
  }
  parameters->socket = rank_perm[parameters->socket];
  parameters->message_sizes =
      (int *)malloc(sizeof(int) * parameters->message_sizes_max);
  if (!parameters->message_sizes)
    goto error;
  for (i = 0; i < msizes_max; i++) {
    parameters->message_sizes[i] = msizes[rank_perm[i]];
  }
  free(msizes);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_in += i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data, parameters->ascii_in);
  if (i <= 0) {
    printf("error reading algorithm rank_permutation\n");
    exit(2);
  }
  for (i = 0; i < data.num_blocks; i++) {
    for (j = 0; j < data.blocks[i].num_lines; j++) {
      data.blocks[i].lines[j].frac = rank_perm[data.blocks[i].lines[j].frac];
      for (k = 0; k < data.blocks[i].lines[j].sendto_max; k++) {
        if (data.blocks[i].lines[j].sendto_node[k] >= 0) {
          data.blocks[i].lines[j].sendto_node[k] = rank_perm[data.blocks[i].lines[j].sendto_node[k]];
        }
      }
      for (k = 0; k < data.blocks[i].lines[j].recvfrom_max; k++) {
        if (data.blocks[i].lines[j].recvfrom_node[k] >= 0) {
          data.blocks[i].lines[j].recvfrom_node[k] = rank_perm[data.blocks[i].lines[j].recvfrom_node[k]];
        }
      }
    }
  }
  nbuffer_out +=
      ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag > 0) {
      nbuffer_out +=
          ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
    }
  } while (flag);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
