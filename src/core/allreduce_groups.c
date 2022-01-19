#include "allreduce_groups.h"
#include "allreduce.h"
#include "constants.h"
#include "read.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int get_gbstep(int group, int *groups, int num_nodes, int *allgatherv){
  int groups_short[num_nodes], gbstep, i, max_group = 0, max_reduce, gbstep_;
  for (i = 0; groups[i]; i++) {
    if (groups[i] < 0) {
      groups_short[max_group++] = abs(groups[i]);
    }
  }
  max_group--;
  for (gbstep = 1, max_reduce = max_group; gbstep < num_nodes; gbstep *= groups_short[max_reduce], max_reduce--);
  *allgatherv = 0;
  if (group <= max_reduce) {
    for (gbstep = 1, i = 0; i < group; gbstep *= groups_short[i], i++);
  } else {
    for (gbstep = 1, i = max_group; i > group; gbstep *= groups_short[i], i--);
    for (gbstep_ = 1, i = 0; i < group; gbstep_ *= groups_short[i], i++);
    if (gbstep_ >= num_nodes) {
      *allgatherv = 1;
    }
  }
//printf("aaaaaa %d %d %d %d %d\n", max_reduce, max_group, gbstep, group, *allgatherv);
  return gbstep;
}

static int node_local(int node_global, int gbstep, int factor, int *component) {
//printf("bbbbb_l %d\n", node_global);
  *component = (node_global / factor / gbstep) * gbstep + node_global % gbstep;
  return ((node_global / gbstep) % factor);
}

static int node_global(int node_local, int gbstep, int factor, int component) {
//printf("bbbbb_g %d\n", node_local);
  return (node_local * gbstep + component % gbstep +
          (component / gbstep) * factor * gbstep);
}

static int insert_lines(int node, int ngroups, int *size_level0,
                        int **size_level1, struct data_line ***data) {
  return 0;
}

static int communication_partner(int gbstep, int num_ports, int task,
                                 int port) {
  return ((task / gbstep + port + 1) % (num_ports + 1) +
          (task / gbstep / (num_ports + 1)) * (num_ports + 1)) *
             gbstep +
         task % gbstep;
}

static void gen_frac_start_basis(int num_nodes, int node, int *groups,
                           int *frac_start) {
  int frac_start_p[num_nodes], partner, fac, gbstep, i, j;
  if (!groups[0]) {
    frac_start[0] = node;
  } else {
    for (i = 0, gbstep = num_nodes; groups[i]; gbstep /= groups[i++]);
    fac = groups[0];
    for (j = 0; j < fac; j++){
      if (j == 0) {
	partner = node;
      } else {
        partner = communication_partner(gbstep, fac - 1, node, j - 1);
      }
      gen_frac_start_basis(num_nodes, partner, groups + 1, frac_start_p);
//printf("aaaaaaaa %d %d %d %d %d\n", node, gbstep, fac, partner, frac_start_p[0]);
      for (i = 0; i < num_nodes / gbstep / fac; i++) {
	frac_start[i + j * (num_nodes / gbstep / fac)] = frac_start_p[i];
      }
    }
  }
}

static void gen_frac_start(int num_nodes, int node, int *groups,
                           int *frac_start) {
  int group, i, j = 0, k, n, groups_[num_nodes], groups__[num_nodes];
  for (i = 0; groups[i]; i++) {
    if (groups[i] < 0) {
      groups_[j++] = abs(groups[i]);
    }
  }
  groups_[j] = 0;
  for (group = j - 1, n = 1; n < num_nodes; n *= groups_[group--]);
  for (i=0, k = group + 1; groups_[k]; i++, k++){
    groups__[i] = groups_[j - i - 1];
  }
  groups__[i] = 0;
//printf("aaaaa %d %d %d\n", groups__[0], groups__[1], groups__[2]);
  gen_frac_start_basis(num_nodes, node, groups__, frac_start);
  for (i = 0; i < node * num_nodes / abs(groups[0]); i++){
    k = frac_start[num_nodes - 1];
    for (j = num_nodes - 1; j > 0; j--) {
      frac_start[j] = frac_start[j - 1];
    }
    frac_start[0] = k;
  }
for (i=0; i< num_nodes; i++){
//printf("aaaaa %d %d\n", i, frac_start[i]);
}
//exit(9);
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
  return -1;
}

static void frac_multiply(int gbstep, int igroup, int component, int fac, int *size_level0, int **size_level1, struct data_line ***data){
  struct data_line **data_new;
  int *size_level1_new, i, j, k, l;
  size_level1_new = (int *) malloc(*size_level0 * sizeof(int));
  if (fac > 0) {
    for (i = 0; i < *size_level0; i++) {
      size_level1_new[i] = (*size_level1)[i] * fac;
    }
    data_new = (struct data_line **) malloc(*size_level0 * sizeof(struct data_line *));
    for (j = 0; j < *size_level0; j++) {
      data_new[j] = (struct data_line *)malloc(size_level1_new[j] * sizeof(struct data_line));
      for (i = 0; i < (*size_level1)[j]; i++) {
        for (k = 0; k < fac; k++){
          data_new[j][i * fac + k].from_max = (*data)[j][i].from_max;
          data_new[j][i * fac + k].from_node = (int *) malloc(data_new[j][i * fac + k].from_max * sizeof(int));
          data_new[j][i * fac + k].from_line = (int *) malloc(data_new[j][i * fac + k].from_max * sizeof(int));
          data_new[j][i * fac + k].to_max = (*data)[j][i].to_max;
          data_new[j][i * fac + k].to = (int *) malloc(data_new[j][i * fac + k].to_max * sizeof(int));
          data_new[j][i * fac + k].frac = (*data)[j][i].frac;
          data_new[j][i * fac + k].source = (*data)[j][i].source;
          for (l = 0; l < data_new[j][i * fac + k].from_max; l++) {
            data_new[j][i * fac + k].from_node[l] = (*data)[j][i].from_node[l];
            if (data_new[j][i * fac + k].from_node[l] >= 0) {
              data_new[j][i * fac + k].from_node[l] = node_global(data_new[j][i * fac + k].from_node[l], gbstep, igroup, component);
            }
            data_new[j][i * fac + k].from_line[l] = (*data)[j][i].from_line[l] * fac + k;
          }
          for (l = 0; l < data_new[j][i * fac + k].to_max; l++) {
            data_new[j][i * fac + k].to[l] = (*data)[j][i].to[l];
            if (data_new[j][i * fac + k].to[l] >= 0) {
              data_new[j][i * fac + k].to[l] = node_global(data_new[j][i * fac + k].to[l], gbstep, igroup, component);
            }
          }
        }
      }
    }
  } else {
    fac = abs(fac);
    for (i = 0; i < *size_level0; i++) {
      size_level1_new[i] = (*size_level1)[i] / fac;
    }
    data_new = (struct data_line **) malloc(*size_level0 * sizeof(struct data_line *));
    for (j = 0; j < *size_level0; j++) {
      data_new[j] = (struct data_line *)malloc(size_level1_new[j] * sizeof(struct data_line));
      for (i = 0; i < size_level1_new[j]; i++) {
        data_new[j][i].from_max = (*data)[j][i * fac].from_max;
        data_new[j][i].from_node = (int *) malloc(data_new[j][i].from_max * sizeof(int));
        data_new[j][i].from_line = (int *) malloc(data_new[j][i].from_max * sizeof(int));
        data_new[j][i].to_max = (*data)[j][i * fac].to_max;
        data_new[j][i].to = (int *) malloc(data_new[j][i].to_max * sizeof(int));
        data_new[j][i].frac = (*data)[j][i * fac].frac;
        data_new[j][i].source = (*data)[j][i * fac].source;
        for (l = 0; l < data_new[j][i].from_max; l++) {
          data_new[j][i].from_node[l] = (*data)[j][i * fac].from_node[l];
          if (data_new[j][i].from_node[l] >=0) {
            data_new[j][i].from_node[l] = node_global(data_new[j][i].from_node[l], gbstep, igroup, component);
          }
          data_new[j][i].from_line[l] = (*data)[j][i * fac].from_line[l] / fac;
        }
        for (l = 0; l < data_new[j][i].to_max; l++) {
          data_new[j][i].to[l] = (*data)[j][i * fac].to[l];
          if (data_new[j][i].to[l] >= 0) {
            data_new[j][i].to[l] = node_global(data_new[j][i].to[l], gbstep, igroup, component);
          }
        }
      }
    }
  }
  ext_mpi_delete_algorithm(*size_level0, *size_level1, *data);
  *size_level1 = size_level1_new;
  *data = data_new;
}

static void get_node_frac(struct parameters_block *parameters, int node, int group, int group_core, struct parameters_block **parameters2, int *node_translation);

static void get_node_start(struct parameters_block *parameters, int node, int group, int group_core, struct parameters_block **parameters2, int *size_level0, int **size_level1, struct data_line ***data, int *node_start) {
  int node_translation_local[parameters->num_nodes], i, j, k, l, m, ngroups, gbstep, num_port, igroup, ports_chunk, component, flag_allgatherv;
  if (group == group_core) {
    get_node_frac(parameters, node, group, group_core, parameters2, node_translation_local);
    for (i = 0; i < size_level1[group][0]; i++) {
      node_start[i] = node_translation_local[data[group_core][0][i].frac];
    }
  } else if (group < group_core) {
    get_node_start(parameters, node, group + 1, group_core, parameters2, size_level0, size_level1, data, node_translation_local);
    for (i = 0; i < size_level1[group][size_level0[group] - 1]; i++) {
      node_start[i] = node_translation_local[i];
    }
    for (j = size_level0[group] - 2; j >= 0; j--) {
      for (k = size_level1[group][j + 1]; k < size_level1[group][j]; k += size_level1[group][size_level0[group] - 1]) {
        ngroups = 0;
        for (l = 0; parameters->groups[l]; l++) {
          if (parameters->groups[l] < 0) {
            ngroups++;
          }
        }
        m = parameters->node;
        parameters->node = node;
/*        num_port = 0;
        for (l = 0; l < ngroups; l++) {
          for (ports_chunk = 0; parameters->groups[num_port + ports_chunk] > 0; ports_chunk++)
            ;
          ports_chunk++;
          igroup = abs(parameters->groups[num_port]);
          gbstep = get_gbstep(l, parameters->groups, parameters->num_nodes, &flag_allgatherv);
          parameters2[l]->node = node_local(data[group][j][k].to[0], gbstep, igroup, &component);
          num_port += ports_chunk;
        }*/
        get_node_start(parameters, data[group][j][k].to[0], group + 1, group_core, parameters2, size_level0, size_level1, data, node_translation_local); 
/*        num_port = 0;
        for (l = 0; l < ngroups; l++) {
          for (ports_chunk = 0; parameters->groups[num_port + ports_chunk] > 0; ports_chunk++)
            ;
          ports_chunk++;
          igroup = abs(parameters->groups[num_port]);
          gbstep = get_gbstep(l, parameters->groups, parameters->num_nodes, &flag_allgatherv);
          parameters2[l]->node = node_local(parameters->node, gbstep, igroup, &component);
          num_port += ports_chunk;
        }*/
        parameters->node = m;
if (group == 0){
printf("kkkkkk %d %d %d\n", j, k, data[group][j][k].to[0]);
for (i=0; i<33; i++){
printf("llllll %d %d\n", i, node_translation_local[i]);
}
}
        for (i = 0; i < size_level1[group][size_level0[group] - 1]; i++) {
          node_start[k + i] = node_translation_local[i];
        }
      }
    }
if (group == 0){
for (i=0; i<size_level1[group][size_level0[group] - 1]; i++){
printf("qqqqqq %d %d %d\n", i, node_start[i], node_translation_local[i]);
}
}
  } else {
  }
}

static void get_node_frac(struct parameters_block *parameters, int node, int group, int group_core, struct parameters_block **parameters2, int *node_translation){
  int node_translation_local[parameters->num_nodes], gbstep, factor, flag_allgatherv, component, num_port, ports_chunk, i, j, k;
  num_port = 0;
  for (i = 0; i <= group; i++) {
    gbstep = get_gbstep(group, parameters->groups, parameters->num_nodes, &flag_allgatherv);
    for (ports_chunk = 0; parameters->groups[num_port + ports_chunk] > 0; ports_chunk++)
      ;
    ports_chunk++;
    factor = abs(parameters->groups[num_port]);
    num_port += ports_chunk;
  }
  gbstep = get_gbstep(group, parameters->groups, parameters->num_nodes, &flag_allgatherv);
  if (group == group_core) {
    node_local(node, gbstep, factor, &component);
    for (j = 0; j < parameters->num_nodes / factor; j++) {
      for (i = 0; i < factor; i++) {
        node_translation[i + j * factor] = node_global(i, gbstep, factor, (component + j) % (parameters->num_nodes / factor));
      }
    }
  } else if (group < group_core) {
    get_node_frac(parameters, node, group + 1, group_core, parameters2, node_translation_local);
    for (i = group + 1, k = parameters2[group + 1]->num_nodes; i < group_core; k *= parameters2[++i]->num_nodes);
    for (j = 0; j < parameters->num_nodes / k; j++) {
      for (i = 0; i < k; i++) {
        node_translation[i + j * k] = node_translation_local[i + j * k];
      }
    }
  } else {
    get_node_frac(parameters, node, group - 1, group_core, parameters2, node_translation_local);
    for (i = 0; i < parameters->num_nodes; i++) {
      node_translation[i] = node_translation_local[i];
    }
  }
}

static void set_frac(struct parameters_block *parameters, int group, int group_core, struct parameters_block **parameters2, int *size_level0, int **size_level1, struct data_line ***data){
  int node_translation[parameters->num_nodes], i, j;
  get_node_frac(parameters, parameters->node, group, group_core, parameters2, node_translation);
  if (group == group_core) {
    for (j = 0; j < size_level0[group]; j++) {
      for (i = 0; i < size_level1[group][j]; i++) {
        data[group][j][i].frac = node_translation[data[group][j][i].frac];
      }
    }
  } else if (group < group_core) {
    get_node_start(parameters, parameters->node, group, group_core, parameters2, size_level0, size_level1, data, node_translation);
    for (j = 0; j < size_level0[group]; j++) {
      for (i = 0; i < size_level1[group][j]; i++) {
        data[group][j][i].frac = node_translation[i];
      }
    }
  } else {
  }
}

static int gen_core(char *buffer_in, int node, struct parameters_block ***parameters2, int *group_core, int **size_level0_l, int ***size_level1_l, struct data_line ****data_l) {
  int nbuffer_out = 0, nbuffer_in = 0, num_port,
      group, ngroups = 0, igroup = -1, num_nodes_start, flag_allgatherv;
  int i, j, gbstep, component, ports_chunk, node_l, num_nodes_l;
  int *frac_start = NULL, *frac_temp = NULL, fac, nnodes_core = INT_MAX;
  struct parameters_block *parameters = NULL;
  char *buffer_in_temp = NULL, *buffer_out_temp = NULL;
  *group_core = -1;
  *parameters2 = NULL;
  *size_level0_l = NULL;
  *size_level1_l = NULL;
  *data_l = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  if (node >= 0) {
    parameters->node = node;
  }
  ngroups = 0;
  for (group = 0; parameters->groups[group]; group++) {
    if (parameters->groups[group] < 0) {
      ngroups++;
    }
  }
  buffer_in_temp = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer_in_temp)
    goto error;
  buffer_out_temp = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer_out_temp)
    goto error;
  frac_start = (int *)malloc((parameters->num_nodes + 1) * sizeof(int));
  if (!frac_start)
    goto error;
  frac_temp = (int *)malloc(parameters->num_nodes * sizeof(int));
  if (!frac_temp)
    goto error;
  *parameters2 = (struct parameters_block **) malloc(ngroups * sizeof(struct parameters_block *));
  *size_level0_l = (int *) malloc(ngroups * sizeof(int));
  *size_level1_l = (int **) malloc(ngroups * sizeof(int *));
  *data_l = (struct data_line ***) malloc(ngroups * sizeof(struct data_line **));
  num_nodes_start = 1;
  num_port = 0;
  for (group = 0; group < ngroups; group++) {
    for (ports_chunk = 0; parameters->groups[num_port + ports_chunk] > 0; ports_chunk++)
      ;
    ports_chunk++;
    if (parameters->num_ports[num_port] < 0) {
      num_nodes_start *= abs(parameters->groups[num_port]);
    }
    num_port += ports_chunk;
  }
  num_port = 0;
  for (group = 0; group < ngroups; group++) {
    gbstep = get_gbstep(group, parameters->groups, parameters->num_nodes, &flag_allgatherv);
    for (ports_chunk = 0; parameters->groups[num_port + ports_chunk] > 0; ports_chunk++)
      ;
    ports_chunk++;
    i = ext_mpi_read_parameters(buffer_in, &(*parameters2)[group]);
    if (i < 0)
      goto error;
    for (i = 0; i < ports_chunk; i++) {
      (*parameters2)[group]->num_ports[i] = parameters->num_ports[num_port + i];
    }
    (*parameters2)[group]->num_ports[i] = 0;
    igroup = abs(parameters->groups[num_port]);
    node_l = node_local(parameters->node, gbstep, igroup, &component);
    num_nodes_l = igroup;
    for (i = 0; i < ports_chunk; i++) {
      (*parameters2)[group]->groups[i] = num_nodes_l;
    }
    (*parameters2)[group]->groups[i - 1] *= -1;
    (*parameters2)[group]->groups[i] = 0;
    (*parameters2)[group]->message_sizes_max = num_nodes_l;
    (*parameters2)[group]->num_nodes = num_nodes_l;
    (*parameters2)[group]->node = node_local(parameters->node, gbstep, igroup, &component);
    if (flag_allgatherv) {
      (*parameters2)[group]->collective_type = collective_type_allgatherv;
    }
    ext_mpi_write_parameters((*parameters2)[group], buffer_in_temp);
    nbuffer_out += ext_mpi_generate_allreduce(buffer_in_temp, buffer_out_temp);
    i = ext_mpi_read_parameters(buffer_out_temp, &(*parameters2)[group]);
    if (i <= 0) goto error;
    i = ext_mpi_read_algorithm(buffer_out_temp + i, &(*size_level0_l)[group], &(*size_level1_l)[group], &(*data_l)[group], (*parameters2)[group]->ascii_in);
    if (i <= 0) goto error;
    if ((*size_level1_l)[group][0] >= num_nodes_start) {
      fac = -(*size_level1_l)[group][0] / num_nodes_start;
    } else {
      fac = num_nodes_start / (*size_level1_l)[group][0];
    }
    frac_multiply(gbstep, igroup, component, fac, &(*size_level0_l)[group], &(*size_level1_l)[group], &(*data_l)[group]);
    num_nodes_start = 0;
    for (i = 0; i < (*size_level1_l)[group][(*size_level0_l)[group] - 1]; i++){
      for (j = 0; j < (*data_l)[group][(*size_level0_l)[group] - 1][i].to_max; j++){
        if ((*data_l)[group][(*size_level0_l)[group] - 1][i].to[j] == -1) {
          num_nodes_start++;
        }
      }
    }
    if ((*size_level1_l)[group][0] < nnodes_core) {
      *group_core = group;
      nnodes_core = (*size_level1_l)[group][0];
    }
    num_port += ports_chunk;
  }
  free(frac_temp);
  free(frac_start);
  free(buffer_out_temp);
  free(buffer_in_temp);
  ext_mpi_delete_parameters(parameters);
  return 0;
error:
//  ext_mpi_delete_algorithm(size_level0_l, size_level1_l, data_l);
  free(size_level1_l);
  free(data_l);
  free(frac_temp);
  free(frac_start);
  free(buffer_out_temp);
  free(buffer_in_temp);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}

int ext_mpi_generate_allreduce_groups(char *buffer_in, char *buffer_out) {
  int node, num_nodes, flag_allgatherv, node_rank,
      node_row_size = 1, node_column_size = 1, copy_method = 0;
  int nbuffer_out = 0, nbuffer_in = 0, num_port,
      group, ngroups = 0, igroup = -1, num_nodes_start;
  int i, j, *size_level0_l = NULL, **size_level1_l = NULL, gbstep, component,
      ports_chunk, node_l, num_nodes_l, *counts = NULL, counts_max;
  int fac, group_core = -1, nnodes_core = INT_MAX;
  struct parameters_block *parameters = NULL, **parameters2 = NULL;
  struct data_line ***data_l = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  if (i < 0)
    goto error;
  ngroups = 0;
  for (group = 0; parameters->groups[group]; group++) {
    if (parameters->groups[group] < 0) {
      ngroups++;
    }
  }
  gen_core(buffer_in, -1, &parameters2, &group_core, &size_level0_l, &size_level1_l, &data_l);
  for (group = 0; group < ngroups; group++) {
    set_frac(parameters, group, group_core, parameters2, size_level0_l, size_level1_l, data_l);
    nbuffer_out += ext_mpi_write_parameters(parameters2[group], buffer_out + nbuffer_out);
    ext_mpi_delete_parameters(parameters2[group]);
    nbuffer_out += ext_mpi_write_algorithm(size_level0_l[group], size_level1_l[group], data_l[group], buffer_out + nbuffer_out, parameters->ascii_out);
    ext_mpi_delete_algorithm(size_level0_l[group], size_level1_l[group], data_l[group]);
  }
  free(data_l);
  free(size_level1_l);
  free(size_level0_l);
  free(parameters2);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
//  ext_mpi_delete_algorithm(size_level0_l, size_level1_l, data_l);
  free(size_level1_l);
  free(data_l);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
