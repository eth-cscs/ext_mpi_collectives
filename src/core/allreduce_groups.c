#include "allreduce_groups.h"
#include "allreduce.h"
#include "constants.h"
#include "read.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int node_local(int node_global, int gbstep, int factor, int *component) {
  *component = (node_global / factor / gbstep) * gbstep + node_global % gbstep;
  return ((node_global / gbstep) % factor);
}

static int node_global(int node_local, int gbstep, int factor, int component) {
  return (node_local * gbstep + component % gbstep +
          (component / gbstep) * factor * gbstep);
}

static int insert_lines(int node, int ngroups, int *size_level0,
                        int **size_level1, struct data_line ***data) {
  struct data_line *data_new;
  int group, shift, step, i, j;
  for (group = 1; group < ngroups; group++) {
    shift = size_level1[group - 1][size_level0[group - 1] - 1] -
            size_level1[group][0];
    for (step = 1; step < size_level0[group]; step++) {
      data_new = (struct data_line *)malloc((size_level1[group][step] + shift) *
                                            sizeof(struct data_line));
      if (!data_new)
        goto error;
      for (i = 0; i < size_level1[group][step] + shift; i++) {
        data_new[i].to = NULL;
        data_new[i].from_node = NULL;
        data_new[i].from_line = NULL;
      }
      for (i = 0; i < shift; i++) {
        data_new[i].frac = data[group - 1][size_level0[group - 1] - 1][i].frac;
        data_new[i].source =
            data[group - 1][size_level0[group - 1] - 1][i].source;
        data_new[i].to_max =
            data[group - 1][size_level0[group - 1] - 1][i].to_max;
        data_new[i].from_max =
            data[group - 1][size_level0[group - 1] - 1][i].from_max;
        data_new[i].to = (int *)malloc(data_new[i].to_max * sizeof(int));
        if (!data_new[i].to)
          goto error;
        for (j = 0; j < data_new[i].to_max; j++) {
          data_new[i].to[j] =
              data[group - 1][size_level0[group - 1] - 1][i].to[j];
        }
        data_new[i].from_node =
            (int *)malloc(data_new[i].from_max * sizeof(int));
        if (!data_new[i].from_node)
          goto error;
        data_new[i].from_line =
            (int *)malloc(data_new[i].from_max * sizeof(int));
        if (!data_new[i].from_line)
          goto error;
        data_new[i].from_max = 0;
        for (j = 0; j < data[group - 1][size_level0[group - 1] - 1][i].from_max;
             j++) {
          if (data[group - 1][size_level0[group - 1] - 1][i].from_node[j] ==
              node) {
            data_new[i].from_node[data_new[i].from_max] =
                data[group - 1][size_level0[group - 1] - 1][i].from_node[j];
            data_new[i].from_line[data_new[i].from_max] =
                data[group - 1][size_level0[group - 1] - 1][i].from_line[j];
            data_new[i].from_max++;
          }
        }
      }
      for (i = 0; i < size_level1[group][step]; i++) {
        data_new[i + shift].frac = data[group][step][i].frac;
        data_new[i + shift].source = data[group][step][i].source;
        data_new[i + shift].to_max = data[group][step][i].to_max;
        data_new[i + shift].from_max = data[group][step][i].from_max;
        data_new[i + shift].to =
            (int *)malloc(data_new[i + shift].to_max * sizeof(int));
        if (!data_new[i + shift].to)
          goto error;
        for (j = 0; j < data_new[i + shift].to_max; j++) {
          data_new[i + shift].to[j] = data[group][step][i].to[j];
        }
        data_new[i + shift].from_node =
            (int *)malloc(data_new[i + shift].from_max * sizeof(int));
        if (!data_new[i + shift].from_node)
          goto error;
        data_new[i + shift].from_line =
            (int *)malloc(data_new[i + shift].from_max * sizeof(int));
        if (!data_new[i + shift].from_line)
          goto error;
        for (j = 0; j < data_new[i + shift].from_max; j++) {
          data_new[i + shift].from_node[j] = data[group][step][i].from_node[j];
          data_new[i + shift].from_line[j] =
              data[group][step][i].from_line[j] + shift;
        }
      }
      for (i = 0; i < size_level1[group][step]; i++) {
        free(data[group][step][i].to);
        free(data[group][step][i].from_node);
        free(data[group][step][i].from_line);
      }
      free(data[group][step]);
      size_level1[group][step] += shift;
      data[group][step] = data_new;
    }
  }
  return 0;
error:
  if (data_new) {
    for (i = 0; i < size_level1[group][step] + shift; i++) {
      free(data_new[i].to);
      free(data_new[i].from_node);
      free(data_new[i].from_line);
    }
  }
  free(data_new);
  return ERROR_MALLOC;
}

static void gen_frac_start(int num_nodes, int node, int *groups,
                           int *frac_start) {
  int group, i, j = 0, k, l, n, groups_[num_nodes];
  for (i = 0; groups[i]; i++) {
    if (groups[i] < 0) {
      groups_[j++] = abs(groups[i]);
    }
  }
  groups_[j] = 0;
  for (i = 0; i < num_nodes; i++) {
    frac_start[i] = i;
  }
  n = num_nodes;
  for (group = 0; groups_[group] && (n > 1); group++) {
    for (i = 0;
         i < ((node / (num_nodes / n)) % groups_[group]) * (n / groups_[group]);
         i++) {
      for (l = 0; l < num_nodes / n; l++) {
        k = frac_start[l * n];
        for (j = 0; j < n - 1; j++) {
          frac_start[j + l * n] = frac_start[j + 1 + l * n];
        }
        frac_start[n - 1 + l * n] = k;
      }
    }
    n /= groups_[group];
  }
}

static int frac_translate(int num_nodes, int *groups, int node_from,
                          int node_to, int frac) {
  int frac_from[num_nodes], frac_to[num_nodes], i;
  gen_frac_start(num_nodes, node_from, groups, frac_from);
  gen_frac_start(num_nodes, node_to, groups, frac_to);
  for (i = 0; i < num_nodes; i++) {
    if (frac_to[i] == frac) {
      return (frac_from[i]);
    }
  }
  return (-1);
}

static int print_algorithm(char *buffer, int node, int num_nodes_l,
                           int node_rank, int node_row_size,
                           int node_column_size, int copy_method, int *counts,
                           int counts_max, int *num_ports, int *groups, int *msizes,
                           int msizes_max, enum edata_type data_type,
                           int num_nodes, int size_level0, int *size_level1,
                           struct data_line **data, int stage_offset,
                           int size_level0_old, int *size_level1_old,
                           struct data_line **data_old, int allgatherv,
                           int ascii_out, int root, int bit_identical, int blocking) {
  struct parameters_block parameters;
  int i, j, stage, nbuffer = 0;
  char *buffer_temp;
  if (stage_offset == 0) {
    if (allgatherv) {
      parameters.collective_type = collective_type_allgatherv;
    } else {
      parameters.collective_type = collective_type_allreduce_group;
    }
    parameters.node = node;
    parameters.num_nodes = num_nodes_l;
    parameters.node_rank = node_rank;
    parameters.node_row_size = node_row_size;
    parameters.node_column_size = node_column_size;
    parameters.copy_method = copy_method;
    parameters.counts_max = counts_max;
    parameters.counts = counts;
    for (parameters.num_ports_max = 0; num_ports[parameters.num_ports_max];
         parameters.num_ports_max++)
      ;
    parameters.num_ports = num_ports;
    parameters.message_sizes_max = msizes_max;
    parameters.message_sizes = msizes;
    parameters.data_type = data_type;
    parameters.groups_max = parameters.num_ports_max;
    parameters.groups = groups;
    parameters.rank_perm_max = 0;
    parameters.rank_perm = NULL;
    parameters.iocounts_max = 0;
    parameters.iocounts = NULL;
    parameters.verbose = 0;
    parameters.bit_identical = bit_identical;
    parameters.ascii_out = 1;
    parameters.locmem_max = -1;
    parameters.shmem_max = -1;
    parameters.shmem_buffer_offset_max = 0;
    parameters.shmem_buffer_offset = NULL;
    parameters.root = root;
    parameters.in_place = 0;
    parameters.blocking = blocking;
    if (ascii_out) {
      nbuffer += ext_mpi_write_parameters(&parameters, buffer + nbuffer);
    } else {
      ext_mpi_write_parameters(&parameters, buffer + nbuffer);
      buffer_temp = strrchr(buffer + nbuffer, '\n');
      buffer_temp[0] = '\0';
      buffer_temp = strrchr(buffer + nbuffer, '\n');
      buffer_temp[1] = '\0';
      nbuffer = strlen(buffer);
    }
    if (size_level1) {
      for (i = 0; i < size_level1[0]; i++) {
        nbuffer += sprintf(buffer + nbuffer, " STAGE %d FRAC %d SOURCE %d TO",
                           0, data[0][i].frac, data[0][i].source);
        for (j = 0; j < data[0][i].to_max; j++) {
          nbuffer += sprintf(buffer + nbuffer, " %d", data[0][i].to[j]);
        }
        nbuffer += sprintf(buffer + nbuffer, " FROM");
        if (stage_offset > 0) {
          for (j = 0; j < data_old[size_level0_old - 1][i].from_max; j++) {
            nbuffer += sprintf(buffer + nbuffer, " %d|%d",
                               data_old[size_level0_old - 1][i].from_node[j],
                               data_old[size_level0_old - 1][i].from_line[j]);
          }
        } else {
          for (j = 0; j < data[0][i].from_max; j++) {
            nbuffer +=
                sprintf(buffer + nbuffer, " %d|%d", data[0][i].from_node[j],
                        data[0][i].from_line[j]);
          }
        }
        nbuffer += sprintf(buffer + nbuffer, "\n");
      }
      nbuffer += sprintf(buffer + nbuffer, "#\n");
    }
  }
  for (stage = 1; stage < size_level0; stage++) {
    for (i = 0; i < size_level1[stage]; i++) {
      nbuffer += sprintf(buffer + nbuffer, " STAGE %d FRAC %d SOURCE %d TO",
                         stage + stage_offset, data[stage][i].frac,
                         data[stage][i].source);
      for (j = 0; j < data[stage][i].to_max; j++) {
        nbuffer += sprintf(buffer + nbuffer, " %d", data[stage][i].to[j]);
      }
      nbuffer += sprintf(buffer + nbuffer, " FROM");
      if ((stage == 0) && (stage_offset > 0)) {
        for (j = 0; j < data_old[size_level0_old - 1][i].from_max; j++) {
          nbuffer += sprintf(buffer + nbuffer, " %d|%d",
                             data_old[size_level0_old - 1][i].from_node[j],
                             data_old[size_level0_old - 1][i].from_line[j]);
        }
      } else {
        for (j = 0; j < data[stage][i].from_max; j++) {
          nbuffer +=
              sprintf(buffer + nbuffer, " %d|%d", data[stage][i].from_node[j],
                      data[stage][i].from_line[j]);
        }
      }
      nbuffer += sprintf(buffer + nbuffer, "\n");
    }
    nbuffer += sprintf(buffer + nbuffer, "#\n");
  }
  for (i = 0; i < num_nodes; i++) {
    nbuffer += sprintf(buffer + nbuffer,
                       " STAGE 0 FRAC %d SOURCE %d TO 0 FROM -1|-1\n", i, node);
  }
  return nbuffer;
}

int ext_mpi_generate_allreduce_groups(char *buffer_in, char *buffer_out) {
  int node, num_nodes, flag, flag2, node_rank,
      node_row_size = 1, node_column_size = 1, stage_offset, copy_method = 0;
  int nbuffer_out = 0, nbuffer_in = 0, *msizes = NULL, msizes_max = 0,
      *msizes_l = NULL, msizes_max_l = 0, *num_ports = NULL, num_port,
      num_ports_max = 0, *groups = NULL, group, ngroups = 0, igroup,
      *num_ports_l = NULL, num_nodes_start, *groups_l=NULL;
  int i, j, k, l, m, n, o, q,
      *size_level0_l = NULL, **size_level1_l = NULL, gbstep, component,
      ports_chunk, node_l, num_nodes_l, *counts = NULL, counts_max, stage = -1;
  int *frac_start = NULL, *frac_temp = NULL, node_o = -1, node_ol = -1, flaggb,
      node_g, comp_t, gbstep_old, igroup_old;
  struct parameters_block *parameters = NULL, *parameters2 = NULL;
  struct data_line ***data_l = NULL;
  char **buffer_in_temp = NULL, **buffer_out_temp = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  node = parameters->node;
  num_nodes = parameters->num_nodes;
  node_row_size = parameters->node_row_size;
  node_column_size = parameters->node_column_size;
  node_rank = parameters->node_rank;
  copy_method = parameters->copy_method;
  num_ports_max = parameters->num_ports_max;
  num_ports = parameters->num_ports;
  groups = parameters->groups;
  counts_max = parameters->counts_max;
  counts = parameters->counts;
  msizes_max = parameters->message_sizes_max;
  msizes = parameters->message_sizes;
  ngroups = 0;
  for (group = 0; groups[group]; group++) {
    if (groups[group] < 0) {
      ngroups++;
    }
  }
  num_ports_l = (int *)malloc((num_ports_max + 1) * sizeof(int));
  if (!num_ports_l)
    goto error;
  groups_l = (int *)malloc((num_ports_max + 1) * sizeof(int));
  if (!groups_l)
    goto error;
  buffer_in_temp = (char **)malloc(ngroups * sizeof(char *));
  if (!buffer_in_temp)
    goto error;
  buffer_out_temp = (char **)malloc(ngroups * sizeof(char *));
  if (!buffer_out_temp)
    goto error;
  for (i = 0; i < ngroups; i++) {
    buffer_in_temp[i] = (char *)malloc(MAX_BUFFER_SIZE);
    if (!buffer_in_temp[i])
      goto error;
    buffer_out_temp[i] = (char *)malloc(MAX_BUFFER_SIZE);
    if (!buffer_out_temp[i])
      goto error;
  }
  num_nodes_start = num_nodes;
  frac_start = (int *)malloc(num_nodes * sizeof(int));
  if (!frac_start)
    goto error;
  frac_temp = (int *)malloc(num_nodes * sizeof(int));
  if (!frac_temp)
    goto error;
  gen_frac_start(num_nodes, node, groups, frac_start);
  size_level0_l = (int *)malloc(ngroups * sizeof(int));
  if (!size_level0_l)
    goto error;
  size_level1_l = (int **)malloc(ngroups * sizeof(int *));
  if (!size_level1_l)
    goto error;
  data_l = (struct data_line ***)malloc(ngroups * sizeof(struct data_line **));
  if (!data_l)
    goto error;
  num_port = 0;
  gbstep = 1;
  stage_offset = 0;
  flag = 0;
  for (group = 0; group < ngroups; group++) {
    for (ports_chunk = 0; groups[num_port + ports_chunk] > 0; ports_chunk++)
      ;
    ports_chunk++;
    for (i = 0; i < ports_chunk; i++) {
      num_ports_l[i] = num_ports[num_port + i];
    }
    num_ports_l[i] = 0;
    if ((gbstep >= num_nodes) && !flag) {
      gbstep = 1;
      flag = 1;
    }
    igroup = abs(groups[num_port]);
    node_l = node_local(node, gbstep, igroup, &component);
    num_nodes_l = igroup;
    msizes_max_l = num_nodes_l;
    msizes_l = (int *)malloc(msizes_max_l * sizeof(int));
    if (!msizes_l)
      return ERROR_MALLOC;
    for (i = 0; i < msizes_max_l; i++) {
      msizes_l[i] = 11;
    }
    for (i = 0; i < ports_chunk; i++) {
      groups_l[i] = num_nodes_l;
    }
    groups_l[i-1]*=-1;
    groups_l[i] = 0;
    print_algorithm(buffer_in_temp[group], node_l, num_nodes_l, node_rank,
                    node_row_size, node_column_size, copy_method, counts,
                    counts_max, num_ports_l, groups_l, msizes_l, msizes_max_l,
                    parameters->data_type, num_nodes_start, 0, NULL, NULL, 0, 0,
                    NULL, NULL, flag, parameters->ascii_out, parameters->root,
                    parameters->bit_identical, parameters->blocking);
    free(msizes_l);
    if (num_ports_l[0] < 0) {
      num_nodes_start /= igroup;
    } else {
      if (flag) {
        num_nodes_start *= igroup;
      }
    }
    ext_mpi_generate_allreduce(buffer_in_temp[group], buffer_out_temp[group]);
    i = ext_mpi_read_parameters(buffer_out_temp[group], &parameters2);
    if (i < 0)
      goto error;
    i = ext_mpi_read_algorithm(buffer_out_temp[group] + i, &size_level0_l[group],
                               &size_level1_l[group], &data_l[group],
                               parameters2->ascii_in);
    if (i == ERROR_MALLOC)
      goto error;
    if (i <= 0) {
      printf("error reading algorithm allreduce_groups b\n");
      exit(2);
    }
    ext_mpi_delete_parameters(parameters2);
    gbstep *= abs(groups[num_port]);
    num_port += ports_chunk;
    stage_offset += size_level0_l[group] - 1;
  }
  num_port = 0;
  gbstep = 1;
  flag = 0;
  for (group = 0; group < ngroups; group++) {
    for (ports_chunk = 0; groups[num_port + ports_chunk] > 0; ports_chunk++)
      ;
    ports_chunk++;
    if ((gbstep >= num_nodes) && !flag) {
      gbstep = 1;
      flag = 1;
    }
    igroup = abs(groups[num_port]);
    node_l = node_local(node, gbstep, igroup, &component);
    num_nodes_l = igroup;
    for (stage = 0; stage < size_level0_l[group]; stage++) {
      for (i = 0; i < size_level1_l[group][stage]; i++) {
        data_l[group][stage][i].source = node_global(
            data_l[group][stage][i].source, gbstep, igroup, component);
      }
    }
    gbstep *= abs(groups[num_port]);
    num_port += ports_chunk;
  }
  for (group = 0; group < ngroups; group++) {
    for (stage = 1; stage < size_level0_l[group]; stage++) {
      for (i = 0; i < size_level1_l[group][stage]; i++) {
        if (stage > 1) {
          if (i < size_level1_l[group][stage - 1]) {
            data_l[group][stage][i].frac = data_l[group][stage - 1][i].frac;
            data_l[group][stage][i].source = data_l[group][stage - 1][i].source;
          }
        } else {
          if (group > 0) {
            if (i < size_level1_l[group][0]) {
              data_l[group][stage][i].frac =
                  data_l[group - 1][size_level0_l[group - 1] - 1]
                        [i - size_level1_l[group][0] +
                         size_level1_l[group - 1][size_level0_l[group - 1] - 1]]
                            .frac;
              data_l[group][stage][i].source =
                  data_l[group - 1][size_level0_l[group - 1] - 1]
                        [i - size_level1_l[group][0] +
                         size_level1_l[group - 1][size_level0_l[group - 1] - 1]]
                            .source;
            }
          }
        }
      }
    }
  }
  num_port = 0;
  gbstep = 1;
  flaggb = 0;
  for (group = 0; group < ngroups; group++) {
    for (ports_chunk = 0; groups[num_port + ports_chunk] > 0; ports_chunk++)
      ;
    ports_chunk++;
    if ((gbstep >= num_nodes) && !flaggb) {
      gbstep = 1;
      flaggb = 1;
    }
    igroup = abs(groups[num_port]);
    node_l = node_local(node, gbstep, igroup, &component);
    for (stage = 1; stage < size_level0_l[group]; stage++) {
      for (i = 0; i < size_level1_l[group][stage]; i++) {
        flag = 1;
        flag2 = 1;
        for (j = 0; (j < data_l[group][stage][i].from_max) && flag2; j++) {
          if ((data_l[group][stage][i].from_node[j] == node_l) && (j == 0)) {
            flag = 0;
          } else {
            flag2 = 0;
            node_ol = data_l[group][stage][i].from_node[j];
            node_o = (igroup + node_l +
                      (igroup + node_l - data_l[group][stage][i].from_node[j]) %
                          igroup) %
                     igroup;
          }
        }
        if (flag) {
          n = 0;
          for (k = 0; k < i; k++) {
            flag2 = 1;
            for (j = 0; (j < data_l[group][stage][k].from_max) && flag2; j++) {
              if (data_l[group][stage][k].from_node[j] == node_ol) {
                n++;
                flag2 = 0;
              }
            }
          }
          flag2 = 1;
          o = -1;
          m = n + 1;
          if (stage > 1) {
            for (k = 0; (k < size_level1_l[group][stage - 1]) && flag2; k++) {
              flag = 1;
              for (l = 0;
                   (l < data_l[group][stage - 1][k].to_max) && flag2 && flag;
                   l++) {
                if (data_l[group][stage - 1][k].to[l] == node_o) {
                  m--;
                  flag = 0;
                }
              }
              if (m == 0) {
                flag2 = 0;
              }
            }
            if (!flag2) {
              node_g = node_global(data_l[group][stage][i].from_node[0], gbstep,
                                   igroup, component);
              o = frac_translate(num_nodes, groups, node_g, node,
                                 data_l[group][stage - 1][k - 1].frac);
            }
          } else {
            if (group > 0) {
              for (k = 0;
                   (k <
                    size_level1_l[group - 1][size_level0_l[group - 1] - 1]) &&
                   flag2;
                   k++) {
                flag = 1;
                for (l = 0;
                     (l < data_l[group - 1][size_level0_l[group - 1] - 1][k]
                              .to_max) &&
                     flag2 && flag;
                     l++) {
                  node_local(node, gbstep_old, igroup_old, &comp_t);
                  if (data_l[group - 1][size_level0_l[group - 1] - 1][k]
                          .to[l] == -1) {
                    m--;
                    flag = 0;
                  }
                }
                if (m == 0) {
                  flag2 = 0;
                }
              }
              if (!flag2) {
                node_g = node_global(data_l[group][stage][i].from_node[0],
                                     gbstep, igroup, component);
                o = frac_translate(
                    num_nodes, groups, node_g, node,
                    data_l[group - 1][size_level0_l[group - 1] - 1][k - 1]
                        .frac);
              }
            }
          }
          if (o >= 0) {
            for (q = stage; q < size_level0_l[group]; q++) {
              data_l[group][q][i].frac = o;
            }
            for (m = group + 1; m < ngroups; m++) {
              for (q = 1; q < size_level0_l[m]; q++) {
                if ((i < size_level1_l[m][q]) && (i >= 0)) {
                  data_l[m][q][i].frac = o;
                }
              }
            }
          }
          for (j = 0; j < data_l[group][stage][i].from_max; j++) {
            if (data_l[group][stage][i].from_node[j] == node_l) {
              //              data_l[group][stage][i].from_line[j]=i;
              flag = 1;
              if (j > 0) {
                for (k = i - 1;
                     (k >= 0) && (data_l[group][stage][i].from_node[0] ==
                                  data_l[group][stage][k].from_node[0]);
                     k--)
                  ;
              } else {
                k = n - 1;
              }
              for (; (k >= 0) && flag; k--) {
                if (data_l[group][stage][i].frac ==
                    data_l[group][stage][k].frac) {
                  //                  data_l[group][stage][i].from_line[j]=k;
                  flag = 0;
                }
              }
            }
          }
        }
      }
    }
    gbstep_old = gbstep;
    igroup_old = igroup;
    gbstep *= abs(groups[num_port]);
    num_port += ports_chunk;
  }
  for (group = 0; group < ngroups; group++) {
    for (stage = 0; stage < size_level0_l[group]; stage++) {
      for (i = 0; i < size_level1_l[group][stage]; i++) {
        data_l[group][stage][i].frac = frac_start[data_l[group][stage][i].frac];
      }
    }
  }
  num_port = 0;
  gbstep = 1;
  flag = 0;
  for (group = 0; group < ngroups; group++) {
    for (ports_chunk = 0; groups[num_port + ports_chunk] > 0; ports_chunk++)
      ;
    ports_chunk++;
    if ((gbstep >= num_nodes) && !flag) {
      gbstep = 1;
      flag = 1;
    }
    igroup = abs(groups[num_port]);
    node_l = node_local(node, gbstep, igroup, &component);
    num_nodes_l = igroup;
    for (stage = 0; stage < size_level0_l[group]; stage++) {
      for (i = 0; i < size_level1_l[group][stage]; i++) {
        for (j = 0; j < data_l[group][stage][i].to_max; j++) {
          if (data_l[group][stage][i].to[j] >= 0) {
            data_l[group][stage][i].to[j] = node_global(
                data_l[group][stage][i].to[j], gbstep, igroup, component);
          }
        }
        for (j = 0; j < data_l[group][stage][i].from_max; j++) {
          if (data_l[group][stage][i].from_node[j] >= 0) {
            data_l[group][stage][i].from_node[j] =
                node_global(data_l[group][stage][i].from_node[j], gbstep,
                            igroup, component);
          }
        }
      }
      for (i = 0; i < size_level1_l[group][stage]; i++) {
        //        data_l[group][stage][i].source=node_global(data_l[group][stage][i].source,
        //        gbstep, igroup, component);
      }
    }
    gbstep *= abs(groups[num_port]);
    num_port += ports_chunk;
  }
  if (parameters->bit_identical) {
    group = ngroups - 1;
    stage = size_level0_l[group] - 1;
  }
  i = insert_lines(node, ngroups, size_level0_l, size_level1_l, data_l);
  if (i < 0)
    goto error;
  if (parameters->bit_identical && (group > 0)) {
    flag = 1;
    for (j = size_level1_l[group][stage] - 1; (j >= 0) && flag; j--) {
      for (l = 0; l < data_l[group][stage][j].from_max; l++) {
        if (data_l[group][stage][j].from_node[l] != node) {
          l = INT_MAX - 1;
        }
      }
      if (l < INT_MAX - 1) {
        flag = 0;
      }
    }
    j += 2;
    for (i = 0; i < j; i++) {
      data_l[group][stage][i].from_node[0] = node;
      data_l[group][stage][i].from_line[0] = i;
      data_l[group][stage][i].from_max = 1;
      data_l[group][stage][i].to[0] = node;
      data_l[group][stage][i].to_max = 1;
    }
    for (k = 0; k < num_nodes; k++) {
      flag = 1;
      for (i = 0; (i < size_level1_l[group][stage]) && flag; i++) {
        if (data_l[group][stage][i].frac == k) {
          for (l = 0; l < data_l[group][stage][i].from_max; l++) {
            if (data_l[group][stage][i].from_node[l] != node) {
              flag = 0;
            }
          }
        }
      }
      if (flag) {
        j = -1;
        l = INT_MAX;
        for (i = 0; i < size_level1_l[group][stage]; i++) {
          if ((data_l[group][stage][i].frac == k) &&
              (data_l[group][stage][i].source < l)) {
            j = i;
            l = data_l[group][stage][i].source;
            flag = 0;
          }
        }
        if (j >= 0) {
          for (l = 0; l < data_l[group][stage][j].to_max; l++) {
            if (data_l[group][stage][j].to[l] == -1) {
              l = INT_MAX - 1;
            }
          }
          if (l < INT_MAX - 1) {
            data_l[group][stage][j].to[data_l[group][stage][j].to_max] = -1;
            data_l[group][stage][j].to_max++;
          }
        }
      }
    }
  }
  num_port = 0;
  for (group = 0; group < ngroups; group++) {
    for (ports_chunk = 0; groups[num_port + ports_chunk] > 0; ports_chunk++)
      ;
    ports_chunk++;
    for (stage = 0; stage < size_level0_l[group]; stage++) {
      for (i = 0; i < size_level1_l[group][stage]; i++) {
        if (data_l[group][stage][i].from_max == 0) {
          data_l[group][stage][i].from_node[0] = node;
          data_l[group][stage][i].from_line[0] = i;
          data_l[group][stage][i].from_max = 1;
        }
      }
    }
  }
  for (i = 0; i < size_level1_l[0][0]; i++) {
    data_l[0][0][i].source = node;
  }
  num_port = 0;
  gbstep = 1;
  stage_offset = 0;
  flag = 0;
  for (group = 0; group < ngroups; group++) {
    for (ports_chunk = 0; groups[num_port + ports_chunk] > 0; ports_chunk++)
      ;
    ports_chunk++;
    igroup = abs(groups[num_port]);
    if (gbstep * igroup >= num_nodes) {
      gbstep = 1;
      flag = 1;
    }
    node_l = node_local(node, gbstep, igroup, &component);
    num_nodes_l = igroup;
    if (group < ngroups - 1) {
      k = 0;
      for (i = 0; i < size_level1_l[group][size_level0_l[group] - 1]; i++) {
        flag2 = 0;
        for (j = 0; j < data_l[group][size_level0_l[group] - 1][i].to_max;
             j++) {
          if (data_l[group][size_level0_l[group] - 1][i].to[j] == -1) {
            flag2 = 1;
          }
        }
        if (flag2 && (k < size_level1_l[group + 1][0])) {
          free(data_l[group][size_level0_l[group] - 1][i].to);
          data_l[group][size_level0_l[group] - 1][i].to =
              (int *)malloc(data_l[group + 1][0][k].to_max * sizeof(int));
          if (!data_l[group][size_level0_l[group] - 1][i].to)
            goto error;
          data_l[group][size_level0_l[group] - 1][i].to_max = 0;
          for (j = 0; j < data_l[group + 1][0][k].to_max; j++) {
            data_l[group][size_level0_l[group] - 1][i]
                .to[data_l[group][size_level0_l[group] - 1][i].to_max++] =
                data_l[group + 1][0][k].to[j];
          }
          k++;
        }
      }
      if (group > 0) {
        print_algorithm(buffer_out_temp[group], node, num_nodes, node_rank,
                        node_row_size, node_column_size, copy_method, counts,
                        counts_max, num_ports, groups, msizes, msizes_max,
                        parameters->data_type, 0, size_level0_l[group],
                        size_level1_l[group], data_l[group], stage_offset,
                        size_level0_l[group - 1], size_level1_l[group - 1],
                        data_l[group - 1], 0, parameters->ascii_out,
                        parameters->root, parameters->bit_identical, parameters->blocking);
      } else {
        print_algorithm(
            buffer_out_temp[group], node, num_nodes, node_rank, node_row_size,
            node_column_size, copy_method, counts, counts_max, num_ports, groups,
            msizes, msizes_max, parameters->data_type, 0, size_level0_l[group],
            size_level1_l[group], data_l[group], stage_offset, 0, NULL, NULL, 0,
            parameters->ascii_out, parameters->root, parameters->bit_identical, parameters->blocking);
      }
    } else {
      if (group > 0) {
        print_algorithm(buffer_out_temp[group], node, num_nodes, node_rank,
                        node_row_size, node_column_size, copy_method, counts,
                        counts_max, num_ports, groups, msizes, msizes_max,
                        parameters->data_type, 0, size_level0_l[group],
                        size_level1_l[group], data_l[group], stage_offset,
                        size_level0_l[group - 1], size_level1_l[group - 1],
                        data_l[group - 1], 0, parameters->ascii_out,
                        parameters->root, parameters->bit_identical, parameters->blocking);
      } else {
        print_algorithm(
            buffer_out_temp[group], node, num_nodes, node_rank, node_row_size,
            node_column_size, copy_method, counts, counts_max, num_ports, groups,
            msizes, msizes_max, parameters->data_type, 0, size_level0_l[group],
            size_level1_l[group], data_l[group], stage_offset, 0, NULL, NULL, 0,
            parameters->ascii_out, parameters->root, parameters->bit_identical, parameters->blocking);
      }
    }
    gbstep *= abs(groups[num_port]);
    num_port += ports_chunk;
    stage_offset += size_level0_l[group] - 1;
  }
  for (group = 0; group < ngroups; group++) {
    ext_mpi_delete_algorithm(size_level0_l[group], size_level1_l[group], data_l[group]);
  }
  free(size_level0_l);
  free(size_level1_l);
  free(data_l);
  for (group = 0; group < ngroups; group++) {
    nbuffer_out +=
        sprintf(buffer_out + nbuffer_out, "%s", buffer_out_temp[group]);
  }
  for (i = ngroups - 1; i >= 0; i--) {
    free(buffer_out_temp[i]);
    free(buffer_in_temp[i]);
  }
  free(frac_temp);
  free(frac_start);
  free(buffer_out_temp);
  free(buffer_in_temp);
  free(groups_l);
  free(num_ports_l);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  for (group = 0; group < ngroups; group++) {
    ext_mpi_delete_algorithm(size_level0_l[group], size_level1_l[group], data_l[group]);
  }
  free(size_level0_l);
  free(size_level1_l);
  free(data_l);
  for (i = ngroups - 1; i >= 0; i--) {
    if (buffer_out_temp)
      free(buffer_out_temp[i]);
    if (buffer_in_temp)
      free(buffer_in_temp[i]);
  }
  free(frac_temp);
  free(frac_start);
  free(buffer_out_temp);
  free(buffer_in_temp);
  free(groups_l);
  free(num_ports_l);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
