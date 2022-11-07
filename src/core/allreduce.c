#include "allreduce.h"
#include "allreduce_single.h"
#include "constants.h"
#include "read.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int get_ngroups(struct parameters_block *parameters, int dire) {
  int ngroups = 0, group, ports;
  switch (dire) {
    case 0:
      for (group = 0; parameters->groups[group]; group++) {
        if (parameters->groups[group] < 0) {
          ngroups++;
        }
      }
      ngroups -= get_ngroups(parameters, -1) + get_ngroups(parameters, 1);
      break;
    case -1:
      for (ports = 0; parameters->num_ports[ports] < 0; ports++)
        ;
      for (group = ports - 1; group >= 0; group--) {
        if (parameters->groups[group] < 0) {
          ngroups++;
        }
      }
      break;
    case 1:
      for (ports = 0; parameters->num_ports[ports] < 0; ports++)
        ;
      for (group = ports; parameters->groups[group]; group++) {
        if (parameters->groups[group] < 0) {
          ngroups++;
        }
      }
      ngroups--;
      break;
  }
  return ngroups;
}

static int get_num_ports_group(struct parameters_block *parameters, int step, int *num_ports, int *group) {
  int ngroups, ports_group, length = 0, i;
  if (parameters->num_ports[0] == 0) {
    return 0;
  }
  for (ports_group = 0; parameters->num_ports[ports_group] < 0; ports_group++)
    ;
  ports_group--;
  if (step == 0) {
    step++;
    ngroups = get_ngroups(parameters, 0);
    if (ports_group < 0 || ngroups <= 0) {
      return 0;
    }
    if (parameters->groups[ports_group] < 0) {
      return 0;
    }
    for (i = 0; i < step; i++) {
      for (ports_group--; ports_group >= 0 && parameters->groups[ports_group] > 0; ports_group--)
        ;
    }
  } else if (step < 0) {
    step *= -1;
    ngroups = get_ngroups(parameters, -1);
    if (ports_group < 0 || step > ngroups) {
      return 0;
    }
    for (i = 0; i <= step; i++) {
      for (ports_group--; ports_group >= 0 && parameters->groups[ports_group] > 0; ports_group--)
        ;
    }
  } else {
    ngroups = get_ngroups(parameters, 1);
    if (step > ngroups) {
      return 0;
    }
    for (i = 0; i < step; i++) {
      for (ports_group++; parameters->groups[ports_group] > 0; ports_group++)
        ;
    }
  }
  for (ports_group++; parameters->groups[ports_group] > 0; ports_group++, length++) {
    num_ports[length] = parameters->num_ports[ports_group];
    group[length] = parameters->groups[ports_group];
  }
  num_ports[length] = parameters->num_ports[ports_group];
  group[length++] = parameters->groups[ports_group];
  num_ports[length] = group[length] = 0;
  return length;
}

static int node_local(int node_global, int gbstep, int factor, int *component) {
  *component = (node_global / factor / gbstep) * gbstep + node_global % gbstep;
  return ((node_global / gbstep) % factor);
}

static int node_global(int node_local, int gbstep, int factor, int component) {
  return (node_local * gbstep + component % gbstep +
          (component / gbstep) * factor * gbstep);
}

/*static void frac_multiply(int gbstep, int igroup, int component, int fac, int *size_level0, int **size_level1, struct data_line ***data){
  struct data_line **data_new;
  int *size_level1_new, i, j, k, l;
  size_level1_new = (int *) malloc(*size_level0 * sizeof(int));
  for (i = 0; i < *size_level0; i++) {
    size_level1_new[i] = (*size_level1)[i] * fac;
  }
  data_new = (struct data_line **) malloc(*size_level0 * sizeof(struct data_line *));
  for (j = 0; j < *size_level0; j++) {
    data_new[j] = (struct data_line *)malloc(size_level1_new[j] * sizeof(struct data_line));
    for (i = 0; i < (*size_level1)[j]; i++) {
      for (k = 0; k < fac; k++){
        data_new[j][i * fac + k].recvfrom_max = (*data)[j][i].recvfrom_max;
        data_new[j][i * fac + k].recvfrom_node = (int *) malloc(data_new[j][i * fac + k].recvfrom_max * sizeof(int));
        data_new[j][i * fac + k].recvfrom_line = (int *) malloc(data_new[j][i * fac + k].recvfrom_max * sizeof(int));
        data_new[j][i * fac + k].sendto_max = (*data)[j][i].sendto_max;
        data_new[j][i * fac + k].sendto = (int *) malloc(data_new[j][i * fac + k].sendto_max * sizeof(int));
        data_new[j][i * fac + k].frac = (*data)[j][i].frac;
        for (l = 0; l < data_new[j][i * fac + k].recvfrom_max; l++) {
          data_new[j][i * fac + k].recvfrom_node[l] = (*data)[j][i].recvfrom_node[l];
          if (data_new[j][i * fac + k].recvfrom_node[l] >= 0) {
            data_new[j][i * fac + k].recvfrom_node[l] = node_global(data_new[j][i * fac + k].recvfrom_node[l], gbstep, igroup, component);
          }
          data_new[j][i * fac + k].recvfrom_line[l] = (*data)[j][i].recvfrom_line[l] * fac + k;
        }
        for (l = 0; l < data_new[j][i * fac + k].sendto_max; l++) {
          data_new[j][i * fac + k].sendto[l] = (*data)[j][i].sendto[l];
          if (data_new[j][i * fac + k].sendto[l] >= 0) {
            data_new[j][i * fac + k].sendto[l] = node_global(data_new[j][i * fac + k].sendto[l], gbstep, igroup, component);
          }
        }
      }
    }
  }
  ext_mpi_delete_algorithm(*size_level0, *size_level1, *data);
  *size_level1 = size_level1_new;
  *data = data_new;
}*/

int ext_mpi_generate_allreduce(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, group, ngroups = 0, i, k,
      nbuffer_out_temp = 0, nbuffer_in_temp = 0;
  struct parameters_block *parameters = NULL, *parameters2 = NULL;
  struct data_algorithm data_l, data;
  char *buffer_in_temp, *buffer_out_temp;
  buffer_in_temp = (char *)malloc(1000000);
  buffer_out_temp = (char *)malloc(1000000);
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  ngroups = get_ngroups(parameters, -1) + get_ngroups(parameters, 0) + get_ngroups(parameters, 1);
  ext_mpi_read_parameters(buffer_in, &parameters2);
  get_num_ports_group(parameters, 1, parameters2->num_ports, parameters2->groups);
  parameters2->num_sockets = abs(parameters2->groups[0]);
  parameters2->collective_type = collective_type_allgatherv;
  nbuffer_in_temp += ext_mpi_write_parameters(parameters2, buffer_in_temp + nbuffer_in_temp);
  ext_mpi_generate_allreduce_single(buffer_in_temp, buffer_out_temp);
  ext_mpi_delete_parameters(parameters2); parameters2 = NULL;
  nbuffer_out_temp += ext_mpi_read_parameters(buffer_out_temp + nbuffer_out_temp, &parameters2);
  nbuffer_out_temp += ext_mpi_read_algorithm(buffer_out_temp + nbuffer_out_temp, &data_l, parameters2->ascii_in);
  nbuffer_out += ext_mpi_write_parameters(parameters2, buffer_out + nbuffer_out);
  nbuffer_out += ext_mpi_write_algorithm(data_l, buffer_out + nbuffer_out, parameters->ascii_out);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  free(parameters2);
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  free(buffer_out_temp);
  free(buffer_in_temp);
  return nbuffer_out;
error:
//  ext_mpi_delete_algorithm(data_l);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
