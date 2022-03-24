#include "allreduce_hierarchical.h"
#include "allreduce_groups.h"
#include "constants.h"
#include "read.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_SOCKETS

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
  return gbstep;
}

static int get_gbstep_reduce_scatter(int group, int *groups, int num_nodes, int *allgatherv){
  int groups_short[num_nodes], gbstep, i, max_group = 0;
  for (i = 0; groups[i]; i++) {
    if (groups[i] < 0) {
      groups_short[max_group++] = abs(groups[i]);
    }
  }
  *allgatherv = 0;
  for (gbstep = 1, i = 0; i < group; gbstep *= groups_short[i], i++);
  return gbstep;
}

static int node_local(int node_global, int gbstep, int factor, int *component) {
  *component = (node_global / factor / gbstep) * gbstep + node_global % gbstep;
  return ((node_global / gbstep) % factor);
}

static int node_global(int node_local, int gbstep, int factor, int component) {
  return (node_local * gbstep + component % gbstep +
          (component / gbstep) * factor * gbstep);
}

static int get_ngroups(struct parameters_block *parameters) {
  int ngroups = 0, group;
  for (group = 0; parameters->groups[group]; group++) {
    if (parameters->groups[group] < 0) {
      ngroups++;
    }
  }
  return ngroups;
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
          data_new[j][i].from_line[l] = (*data)[j][i * fac].from_line[l] / fac;
        }
        for (l = 0; l < data_new[j][i].to_max; l++) {
          data_new[j][i].to[l] = (*data)[j][i * fac].to[l];
        }
      }
    }
  }
  ext_mpi_delete_algorithm(*size_level0, *size_level1, *data);
  *size_level1 = size_level1_new;
  *data = data_new;
}

static int gen_core(char *buffer_in, int node, struct parameters_block ***parameters2, int *group_core, int **size_level0_l, int ***size_level1_l, struct data_line ****data_l) {
  int nbuffer_out = 0, nbuffer_in = 0, num_port,
      group, ngroups = 0, igroup = -1, num_nodes_start, flag_allgatherv;
  int i, j, gbstep, component, ports_chunk, num_nodes_l;
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
  parameters->node = node;
  ngroups = get_ngroups(parameters);
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
    if (parameters->collective_type == collective_type_reduce_scatter) {
      gbstep = get_gbstep_reduce_scatter(group, parameters->groups, parameters->num_nodes, &flag_allgatherv);
    } else {
      gbstep = get_gbstep(group, parameters->groups, parameters->num_nodes, &flag_allgatherv);
    }
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
    ext_mpi_delete_parameters((*parameters2)[group]);
    nbuffer_out += ext_mpi_generate_allreduce_groups(buffer_in_temp, buffer_out_temp);
    i = ext_mpi_read_parameters(buffer_out_temp, &(*parameters2)[group]);
    if (i <= 0) goto error;
    i = ext_mpi_read_algorithm(buffer_out_temp + i, &(*size_level0_l)[group], &(*size_level1_l)[group], &(*data_l)[group], (*parameters2)[group]->ascii_in);
    if (i <= 0) goto error;
    for (fac = (*size_level1_l)[group][0]; (num_nodes_start % fac) || ((*size_level1_l)[group][0] % fac); fac--);
    frac_multiply(gbstep, igroup, component, -(*size_level1_l)[group][0]/fac, &(*size_level0_l)[group], &(*size_level1_l)[group], &(*data_l)[group]);
    frac_multiply(gbstep, igroup, component, num_nodes_start/fac, &(*size_level0_l)[group], &(*size_level1_l)[group], &(*data_l)[group]);
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
  if (parameters->collective_type == collective_type_reduce_scatter) {
    *group_core = ngroups - 1;
  }
  if (parameters->collective_type == collective_type_allgatherv) {
    *group_core = 0;
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

static void get_node_frac(struct parameters_block *parameters, int node, int group, int group_core, int nlines_core, int *node_translation){
  int factor = -1, factor2 = 1, num_port, ports_chunk, i;
  num_port = 0;
  for (i = 0; i < group_core; i++) {
    for (ports_chunk = 0; parameters->groups[num_port + ports_chunk] > 0; ports_chunk++);
    ports_chunk++;
    factor2 *= abs(parameters->groups[num_port]);
    num_port += ports_chunk;
  }
  factor = abs(parameters->groups[num_port]);
  for (i = 0; i < parameters->num_nodes / factor; i++) {
    node_translation[i] = -1;
  }
  if ((nlines_core > 1) || (parameters->num_nodes / factor2 == 1)) {
    for (i = 0; i < factor; i++) {
      node_translation[i] = (i + (node % (parameters->num_nodes / factor)) * factor) % (nlines_core * factor2);
    }
  } else {
    for (i = 0; i < factor; i++) {
      node_translation[i] = ((node % (parameters->num_nodes / factor)) * 1) % (nlines_core * factor2);
    }
  }
}

static void set_frac_recursive(char *buffer_in, struct parameters_block *parameters, int node, int group, int group_core, struct parameters_block **parameters2, int *size_level0, int **size_level1, struct data_line ***data, int *node_translation_begin, int *node_translation_end) {
  int node_translation_local[parameters->num_nodes], i, j, k, l, m, *size_level0_l = NULL, **size_level1_l = NULL, group_core_l;
  struct parameters_block **parameters2_l = NULL;
  struct data_line ***data_l = NULL;
  if (group == group_core) {
    gen_core(buffer_in, node, &parameters2_l, &group_core_l, &size_level0_l, &size_level1_l, &data_l);
    get_node_frac(parameters, node, group, group_core, size_level1[group_core][0], node_translation_begin);
    for (j = 0; j < size_level0[group]; j++) {
      for (i = 0; i < size_level1[group][j]; i++) {
        data[group][j][i].frac = node_translation_begin[data_l[group][j][i].frac];
      }
    }
    for (i = j = 0; i < size_level1[group][size_level0[group] - 1]; i++) {
      for (l = 0; l < data[group][size_level0[group] - 1][i].to_max; l++) {
        if (data[group][size_level0[group] - 1][i].to[l] == -1) {
          node_translation_end[j++] = node_translation_begin[data_l[group][size_level0[group] - 1][i].frac];
        }
      }
    }
  } else if (group < group_core) {
    gen_core(buffer_in, node, &parameters2_l, &group_core_l, &size_level0_l, &size_level1_l, &data_l);
    set_frac_recursive(buffer_in, parameters, node, group + 1, group_core, parameters2_l, size_level0_l, size_level1_l, data_l, node_translation_begin, node_translation_end);
    for (i = 0; i < size_level1_l[group][size_level0_l[group] - 1]; i++) {
      node_translation_begin[i] = data_l[group + 1][0][i].frac;
    }
    for (j = size_level0[group] - 2; j >= 0; j--) {
      for (k = size_level1[group][j + 1]; k < size_level1[group][j]; k += size_level1[group][size_level0[group] - 1]) {
        for (i = get_ngroups(parameters) - 1; i >= 0; i--) {
          ext_mpi_delete_algorithm(size_level0_l[i], size_level1_l[i], data_l[i]);
          ext_mpi_delete_parameters(parameters2_l[i]);
        }
        free(data_l);
        free(size_level1_l);
        free(size_level0_l);
        free(parameters2_l);
        gen_core(buffer_in, data[group][j][k].to[0], &parameters2_l, &group_core_l, &size_level0_l, &size_level1_l, &data_l);
        set_frac_recursive(buffer_in, parameters, data[group][j][k].to[0], group + 1, group_core, parameters2_l, size_level0_l, size_level1_l, data_l, node_translation_local, node_translation_end); 
        for (i = 0; i < size_level1_l[group][size_level0_l[group] - 1]; i++) {
          node_translation_begin[k + i] = data_l[group + 1][0][i].frac;
        }
      }
    }
    for (j = 0; j < size_level0[group]; j++) {
      for (i = 0; i < size_level1[group][j]; i++) {
        data[group][j][i].frac = node_translation_begin[i];
      }
    }
  } else {
    gen_core(buffer_in, node, &parameters2_l, &group_core_l, &size_level0_l, &size_level1_l, &data_l);
    set_frac_recursive(buffer_in, parameters, node, group - 1, group_core, parameters2_l, size_level0_l, size_level1_l, data_l, node_translation_begin, node_translation_end);
    for (i = j = 0; i < size_level1_l[group - 1][size_level0_l[group - 1] - 1]; i++) {
      for (l = 0; l < data[group - 1][size_level0_l[group - 1] - 1][i].to_max; l++) {
        if (data[group - 1][size_level0_l[group - 1] - 1][i].to[l] == -1) {
          node_translation_end[j++] = data_l[group - 1][size_level0_l[group - 1] - 1][i].frac;
        }
      }
    }
    for (m = 0; m < size_level0[group] - 1; m++) {
      for (k = size_level1[group][m]; k < size_level1[group][m + 1]; k += size_level1[group][0]) {
        for (i = get_ngroups(parameters) - 1; i >= 0; i--) {
          ext_mpi_delete_algorithm(size_level0_l[i], size_level1_l[i], data_l[i]);
          ext_mpi_delete_parameters(parameters2_l[i]);
        }
        free(data_l);
        free(size_level1_l);
        free(size_level0_l);
        free(parameters2_l);
        gen_core(buffer_in, data[group][m + 1][k].from_node[0], &parameters2_l, &group_core_l, &size_level0_l, &size_level1_l, &data_l);
        set_frac_recursive(buffer_in, parameters, data[group][m + 1][k].from_node[0], group - 1, group_core, parameters2_l, size_level0_l, size_level1_l, data_l, node_translation_begin, node_translation_local);
        for (i = j = 0; i < size_level1_l[group - 1][size_level0_l[group - 1] - 1]; i++) {
          for (l = 0; l < data[group - 1][size_level0_l[group - 1] - 1][i].to_max; l++) {
            if (data[group - 1][size_level0_l[group - 1] - 1][i].to[l] == -1) {
              node_translation_end[k + j++] = data_l[group - 1][size_level0_l[group - 1] - 1][i].frac;
            }
          }
        }
      }
    }
    for (j = 0; j < size_level0[group]; j++) {
      for (i = 0; i < size_level1[group][j]; i++) {
        data[group][j][i].frac = node_translation_end[i];
      }
    }
  }
  for (group = get_ngroups(parameters) - 1; group >= 0; group--) {
    ext_mpi_delete_algorithm(size_level0_l[group], size_level1_l[group], data_l[group]);
    ext_mpi_delete_parameters(parameters2_l[group]);
  }
  free(data_l);
  free(size_level1_l);
  free(size_level0_l);
  free(parameters2_l);
}

static void adjust_line_numbers_allgatherv(int node, int size_level0, int *size_level1, struct data_line **data){
  int max_rank, stage, rank, line, i;
  for (stage = 0; stage < size_level0; stage++) {
    max_rank = -1;
    for (i = 0; i < size_level1[stage]; i++) {
      if (data[stage][i].from_node[0] > max_rank) {
        max_rank = data[stage][i].from_node[0];
      }
    }
    for (rank = 0; rank <= max_rank; rank++) {
      line = 0;
      for (i = 0; i < size_level1[stage]; i++) {
        if (data[stage][i].from_node[0] != node) {
          if (data[stage][i].from_node[0] == rank) {
            data[stage][i].from_line[0] = line++;
          }
        }
      }
    }
  }
}

static void set_frac_recursive_allgatherv(char *buffer_in, struct parameters_block *parameters, int node, int group_max, int stage_max, int *size_level0, int **size_level1, struct data_line ***data) {
  int i, j, *size_level0_l = NULL, **size_level1_l = NULL, group_core_l, group, stage;
  struct parameters_block **parameters2_l = NULL;
  struct data_line ***data_l = NULL;
  for (i = 0; i <= group_max; i++) {
    adjust_line_numbers_allgatherv(node, size_level0[i], size_level1[i], data[i]);
  }
  for (group = group_max; group >= 0; group--) {
    if (group == group_max) {
      i = stage_max;
    } else {
      i = size_level0[group] - 1;
    }
    for (stage = i; stage >= 0; stage--) {
      if ((group == 0) && (stage == 0)) {
        for (i = 0; i < size_level1[group][stage]; i++) {
          data[group][stage][i].frac = node;
        }
      } else if ((stage == 1) && (group > 0)) {
        for (i = 0; i < size_level1[group][stage]; i++) {
          if (data[group][stage][i].from_node[0] >= 0) {
            gen_core(buffer_in, data[group][stage][i].from_node[0], &parameters2_l, &group_core_l, &size_level0_l, &size_level1_l, &data_l);
            set_frac_recursive_allgatherv(buffer_in, parameters, data[group][stage][i].from_node[0], group - 1, size_level0[group - 1] - 1, size_level0_l, size_level1_l, data_l);
            data[group][stage][i].frac = data_l[group - 1][size_level0[group - 1] - 1][data[group][stage][i].from_line[0]].frac;
            for (j = get_ngroups(parameters) - 1; j >= 0; j--) {
              ext_mpi_delete_algorithm(size_level0_l[j], size_level1_l[j], data_l[j]);
              ext_mpi_delete_parameters(parameters2_l[j]);
            }
            free(data_l);
            free(size_level1_l);
            free(size_level0_l);
            free(parameters2_l);
          }
        }
      } else {
        for (i = 0; i < size_level1[group][stage]; i++) {
          if (data[group][stage][i].from_node[0] >= 0) {
            gen_core(buffer_in, data[group][stage][i].from_node[0], &parameters2_l, &group_core_l, &size_level0_l, &size_level1_l, &data_l);
            set_frac_recursive_allgatherv(buffer_in, parameters, data[group][stage][i].from_node[0], group, stage - 1, size_level0_l, size_level1_l, data_l);
            data[group][stage][i].frac = data_l[group][stage - 1][data[group][stage][i].from_line[0]].frac;
            for (j = get_ngroups(parameters) - 1; j >= 0; j--) {
              ext_mpi_delete_algorithm(size_level0_l[j], size_level1_l[j], data_l[j]);
              ext_mpi_delete_parameters(parameters2_l[j]);
            }
            free(data_l);
            free(size_level1_l);
            free(size_level0_l);
            free(parameters2_l);
          }
        }
      }
    }
  }
}

static void set_frac_recursive_reduce_scatter(char *buffer_in, struct parameters_block *parameters, int node, int group_min, int stage_min, int *size_level0, int **size_level1, struct data_line ***data) {
  int i, j, *size_level0_l = NULL, **size_level1_l = NULL, group_core_l, group, stage, ngroups;
  struct parameters_block **parameters2_l = NULL;
  struct data_line ***data_l = NULL;
  ngroups = get_ngroups(parameters);
  for (group = group_min; group < ngroups; group++) {
    if (group == group_min) {
      i = stage_min;
    } else {
      i = 1;
    }
    for (stage = i; stage < size_level0[group]; stage++) {
      if ((group == ngroups - 1) && (stage == size_level0[group] - 1)) {
        for (i = 0; i < size_level1[group][stage]; i++) {
          data[group][stage][i].frac = node;
        }
      } else if (stage == size_level0[group] - 1) {
        for (i = 0; i < size_level1[group][stage]; i++) {
          if (data[group + 1][0][i].to[0] >= 0) {
            gen_core(buffer_in, data[group + 1][0][i].to[0], &parameters2_l, &group_core_l, &size_level0_l, &size_level1_l, &data_l);
            set_frac_recursive_reduce_scatter(buffer_in, parameters, data[group + 1][0][i].to[0], group + 1, 1, size_level0_l, size_level1_l, data_l);
            data[group][stage][i].frac = data_l[group + 1][1][i % size_level1_l[group + 1][1]].frac;
            for (j = get_ngroups(parameters) - 1; j >= 0; j--) {
              ext_mpi_delete_algorithm(size_level0_l[j], size_level1_l[j], data_l[j]);
              ext_mpi_delete_parameters(parameters2_l[j]);
            }
            free(data_l);
            free(size_level1_l);
            free(size_level0_l);
            free(parameters2_l);
          }
        }
      } else {
        for (i = 0; i < size_level1[group][stage]; i++) {
          if (data[group][stage][i].to[0] >= 0) {
            gen_core(buffer_in, data[group][stage][i].to[0], &parameters2_l, &group_core_l, &size_level0_l, &size_level1_l, &data_l);
            set_frac_recursive_reduce_scatter(buffer_in, parameters, data[group][stage][i].to[0], group, stage + 1, size_level0_l, size_level1_l, data_l);
            data[group][stage][i].frac = data_l[group][stage + 1][i % size_level1_l[group][stage + 1]].frac;
            for (j = get_ngroups(parameters) - 1; j >= 0; j--) {
              ext_mpi_delete_algorithm(size_level0_l[j], size_level1_l[j], data_l[j]);
              ext_mpi_delete_parameters(parameters2_l[j]);
            }
            free(data_l);
            free(size_level1_l);
            free(size_level0_l);
            free(parameters2_l);
          }
        }
      }
    }
  }
}

static void set_frac(char *buffer_in, struct parameters_block *parameters, int group, int group_core, struct parameters_block **parameters2, int *size_level0, int **size_level1, struct data_line ***data){
  int node_translation_begin[parameters->num_nodes], node_translation_end[parameters->num_nodes];
  if (parameters->collective_type == collective_type_reduce_scatter) {
    set_frac_recursive_reduce_scatter(buffer_in, parameters, parameters->node, group, 0, size_level0, size_level1, data);
  } else if (parameters->collective_type == collective_type_allgatherv) {
    set_frac_recursive_allgatherv(buffer_in, parameters, parameters->node, group, size_level0[group] - 1, size_level0, size_level1, data);
  } else {
    set_frac_recursive(buffer_in, parameters, parameters->node, group, group_core, parameters2, size_level0, size_level1, data, node_translation_begin, node_translation_end);
  }
}

static void merge_groups(struct parameters_block *parameters, int group_core, int *size_level0_l, int **size_level1_l, struct data_line ***data_l, int *size_level0, int **size_level1, struct data_line ***data) {
  int ngroups, group, shift = 0, i, j, k, l, m, n, flag, temp, *ptemp;
  ngroups = get_ngroups(parameters);
  *size_level0 = 0;
  for (group = 0; group < ngroups; group++) {
    if (group == 0) {
      *size_level0 += size_level0_l[group];
    } else {
      *size_level0 += size_level0_l[group] - 1;
    }
  }
  *size_level1 = (int *) malloc(*size_level0 * sizeof(int));
  *data = (struct data_line **) malloc(*size_level0 * sizeof(struct data_line *));
  j = 0;
  for (group = 0; group < ngroups; group++) {
    if (group == 0) {
      i = 0;
    } else {
      i = 1;
      shift += size_level1_l[group - 1][size_level0_l[group - 1] - 1] - size_level1_l[group][0];
    }
    for (; i < size_level0_l[group]; i++) {
      (*size_level1)[j] = size_level1_l[group][i] + shift;
      (*data)[j] = (struct data_line *) malloc((*size_level1)[j] * sizeof(struct data_line));
      for (k = 0; k < shift; k++) {
        (*data)[j][k].source = -2;
        (*data)[j][k].frac = -2;
        (*data)[j][k].to_max = 1;
        (*data)[j][k].to = (int *) malloc(sizeof(int));
        (*data)[j][k].to[0] = parameters->node;
        (*data)[j][k].from_max = 1;
        (*data)[j][k].from_node = (int *) malloc(sizeof(int));
        (*data)[j][k].from_line = (int *) malloc(sizeof(int));
        (*data)[j][k].from_node[0] = parameters->node;
        (*data)[j][k].from_line[0] = k;
      }
      for (k = 0; k < size_level1_l[group][i]; k++) {
        (*data)[j][k + shift].source = data_l[group][i][k].source;
        (*data)[j][k + shift].frac = data_l[group][i][k].frac;
        (*data)[j][k + shift].to_max = data_l[group][i][k].to_max;
        (*data)[j][k + shift].from_max = data_l[group][i][k].from_max;
        (*data)[j][k + shift].to = (int*) malloc((*data)[j][k + shift].to_max*sizeof(int));
        (*data)[j][k + shift].from_node = (int*) malloc((*data)[j][k + shift].from_max*sizeof(int));
        (*data)[j][k + shift].from_line = (int*) malloc((*data)[j][k + shift].from_max*sizeof(int));
        for (m = 0; m < (*data)[j][k + shift].to_max; m++){
          (*data)[j][k + shift].to[m] = data_l[group][i][k].to[m];
        }
        for (m = 0; m < (*data)[j][k + shift].from_max; m++){
          (*data)[j][k + shift].from_node[m] = data_l[group][i][k].from_node[m];
          (*data)[j][k + shift].from_line[m] = data_l[group][i][k].from_line[m];
        }
      }
      if ((group > 0) && ((*size_level1)[j - 1] < (*size_level1)[j])) {
        for (k = 0; k < (*size_level1)[j - 1]; k++) {
          (*data)[j][k].frac = (*data)[j - 1][k].frac;
        }
      }
      for (k = shift; k < (*size_level1)[j]; k++) {
        for (l = 0; l < (*data)[j][k].from_max; l++) {
          if ((*data)[j][k].from_node[l] == parameters->node) {
            (*data)[j][k].from_line[l] += shift;
          }
        }
      }
      if ((group > 0) && (shift > 0)) {
        for (n = 0; n < (*size_level1)[j]; n++) {
	  flag = 1;
          for (l = 0; flag && (l < (*data)[j][n].from_max); l++) {
            if (((*data)[j][n].from_node[l] == parameters->node) && ((*data)[j][n].from_line[l] != n) && ((*data)[j][n].from_line[l] < size_level1_l[group - 1][size_level0_l[group - 1] - 1] + shift)) {
              for (k = 0; flag && (k < (*size_level1)[j - 1]); k++) {
                for (m = 0; flag && (m < (*data)[j - 1][k].to_max); m++) {
	          if (((*data)[j - 1][k].to[m] == -1) && ((*data)[j - 1][k].frac == (*data)[j][n].frac)) {
                    (*data)[j][n].from_line[l] = k;
		    flag = 0;
		  }
		}
              }
            }
          }
        }
      }
      j++;
    }
  }
  j = 0;
  for (group = 0; group < ngroups; group++) {
    if (group == 0) {
      i = 0;
    } else {
      i = 1;
    }
    for (; i < size_level0_l[group]; i++) {
      if ((i == size_level0_l[group] - 1) && (group < ngroups - 1)) {
        for (k = l = 0; k < (*size_level1)[j]; k++) {
          for (m = (*data)[j][k].to_max - 1; m >= 0; m--) {
            if ((*data)[j][k].to[m] == -1) {
              (*data)[j][k].to_max = data_l[group + 1][0][l].to_max;
              free((*data)[j][k].to);
              (*data)[j][k].to = (int *) malloc((*data)[j][k].to_max*sizeof(int));
              for (m = 0; m < (*data)[j][k].to_max; m++) {
                (*data)[j][k].to[m] = data_l[group + 1][0][l].to[m];
              }
              l++;
              m = -1;
            }
          }
        }
      }
      if ((group > group_core)) {
        l = (*size_level1)[j]-size_level1_l[group][size_level0_l[group] - 1];
        for (k = 0; k < size_level1_l[group_core][size_level0_l[group_core] - 1]; k++) {
          for (m = 0; m < data_l[group_core][size_level0_l[group_core] - 1][k].to_max; m++){
            if (data_l[group_core][size_level0_l[group_core] - 1][k].to[m] == -1){
              temp = (*data)[j][k].to_max; (*data)[j][k].to_max = (*data)[j][l].to_max; (*data)[j][l].to_max = temp;
              ptemp = (*data)[j][k].to; (*data)[j][k].to = (*data)[j][l].to; (*data)[j][l].to = ptemp;
              l++;
            }
          }
        }
      }
      j++;
    }
  }
}

int ext_mpi_generate_allreduce_hierarchical(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0,
      group, ngroups = 0, i, k, *size_level0_l = NULL, **size_level1_l = NULL,
      size_level0 = 0, *size_level1 = NULL, group_core = -1;
  struct parameters_block *parameters = NULL, **parameters2 = NULL;
  struct data_line ***data_l = NULL, **data = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  ngroups = get_ngroups(parameters);
  gen_core(buffer_in, parameters->node, &parameters2, &group_core, &size_level0_l, &size_level1_l, &data_l);
//  if (parameters->collective_type == collective_type_reduce_scatter) {
//    for (group = ngroups - 1; group >= 0; group--) {
//      set_frac(buffer_in, parameters, group, group_core, parameters2, size_level0_l, size_level1_l, data_l);
//    }
//  } else {
  for (group = 0; group <= group_core; group++) {
    set_frac(buffer_in, parameters, group, group_core, parameters2, size_level0_l, size_level1_l, data_l);
  }
  for (group = ngroups - 1; group > group_core; group--) {
    set_frac(buffer_in, parameters, group, group_core, parameters2, size_level0_l, size_level1_l, data_l);
  }
//  }
  merge_groups(parameters, group_core, size_level0_l, size_level1_l, data_l, &size_level0, &size_level1, &data);
  if ((parameters->collective_type == collective_type_allreduce) || (parameters->collective_type == collective_type_allreduce_group)) {
    k = parameters->num_nodes / size_level1[0];
//    parameters->message_sizes_max /= k;
    for (i = 0; i < parameters->num_nodes; i++){
      if (i % k == 0) {
        parameters->message_sizes[i / k] = parameters->message_sizes[i];
        if (i != i / k) {
          parameters->message_sizes[i] = 0;
        }
      } else {
        parameters->message_sizes[i / k] += parameters->message_sizes[i];
        if (i != i / k) {
          parameters->message_sizes[i] = 0;
        }
      }
    }
  }
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_out += ext_mpi_write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out, parameters->ascii_out);
  for (group = 0; group < ngroups; group++) {
//    nbuffer_out += ext_mpi_write_parameters(parameters2[group], buffer_out + nbuffer_out);
    ext_mpi_delete_parameters(parameters2[group]);
//    nbuffer_out += ext_mpi_write_algorithm(size_level0_l[group], size_level1_l[group], data_l[group], buffer_out + nbuffer_out, parameters->ascii_out);
    ext_mpi_delete_algorithm(size_level0_l[group], size_level1_l[group], data_l[group]);
  }
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  free(data_l);
  free(size_level1_l);
  free(size_level0_l);
  free(parameters2);
  ext_mpi_delete_algorithm(size_level0, size_level1, data);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
//  ext_mpi_delete_algorithm(size_level0_l, size_level1_l, data_l);
  free(size_level1_l);
  free(data_l);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
