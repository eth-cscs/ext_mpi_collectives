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

static int socket_local(int socket_global, int step, int gbstep, int factor, int *component) {
  if (socket_global >= 0) {
    *component = (socket_global / factor / gbstep) * gbstep + socket_global % gbstep;
    return (socket_global / gbstep) % factor;
  } else {
    return socket_global;
  }
}

static int socket_global(int socket_local, int step, int gbstep, int factor, int component) {
  if (socket_local >= 0) {
    return socket_local * gbstep + component % gbstep +
            (component / gbstep) * factor * gbstep;
  } else {
    return socket_local;
  }
}

static void revise_partners(int step, int num_sockets, int num_sockets_l, int component, struct data_algorithm *data) {
  int i, j, k;
  for (i = 0; i < data->num_blocks; i++) {
    for (j = 0; j < data->blocks[i].num_lines; j++) {
      for (k = 0; k < data->blocks[i].lines[j].sendto_max; k++) {
        data->blocks[i].lines[j].sendto[k] = socket_global(data->blocks[i].lines[j].sendto[k], step, num_sockets / num_sockets_l, num_sockets_l, component);
      }
      for (k = 0; k < data->blocks[i].lines[j].recvfrom_max; k++) {
        data->blocks[i].lines[j].recvfrom_node[k] = socket_global(data->blocks[i].lines[j].recvfrom_node[k], step, num_sockets / num_sockets_l, num_sockets_l, component);
      }
    }
  }
}

static int expand_frac(struct parameters_block *parameters, int socket, int num_sockets, int step, int *fracs) {
  int num_frac, i;
  int num_ports[100], group[100];
  if (step == 0) {
    get_num_ports_group(parameters, step, num_ports, group);
    for (num_frac = 0; num_frac < abs(group[0]); num_frac++) {
      fracs[num_frac] = (num_frac + socket) % num_sockets;
    }
  } else if (step < 0) {
    get_num_ports_group(parameters, step + 1, num_ports, group);
    for (i = 0; i < abs(group[0]); i++) {
      expand_frac(parameters, (i + socket) % abs(group[0]), abs(group[0]), step + 1, fracs + i * abs(group[0]));
    }
  } else {
    get_num_ports_group(parameters, step - 1, num_ports, group);
    for (i = 0; i < abs(group[0]); i++) {
      expand_frac(parameters, (i + socket) % abs(group[0]), abs(group[0]), step - 1, fracs + i * abs(group[0]));
    }
  }
  return num_frac;
}

static void frac_multiply(int fac, struct data_algorithm *data){
  struct data_algorithm data_new;
  int i, j, k, l;
  data_new.num_blocks = data->num_blocks;
  data_new.blocks = (struct data_algorithm_block *) malloc(data_new.num_blocks * sizeof(struct data_algorithm_block));
  for (j = 0; j < data_new.num_blocks; j++) {
    data_new.blocks[j].num_lines = data->blocks[j].num_lines * fac;
    data_new.blocks[j].lines = (struct data_algorithm_line *)malloc(data_new.blocks[j].num_lines * sizeof(struct data_algorithm_line));
    memset(data_new.blocks[j].lines, 0, data_new.blocks[j].num_lines * sizeof(struct data_algorithm_line));
    for (i = 0; i < data->blocks[j].num_lines; i++) {
      for (k = 0; k < fac; k++){
        data_new.blocks[j].lines[i * fac + k].recvfrom_max = data->blocks[j].lines[i].recvfrom_max;
        data_new.blocks[j].lines[i * fac + k].recvfrom_node = (int *) malloc(data_new.blocks[j].lines[i * fac + k].recvfrom_max * sizeof(int));
        data_new.blocks[j].lines[i * fac + k].recvfrom_line = (int *) malloc(data_new.blocks[j].lines[i * fac + k].recvfrom_max * sizeof(int));
        data_new.blocks[j].lines[i * fac + k].sendto_max = data->blocks[j].lines[i].sendto_max;
        data_new.blocks[j].lines[i * fac + k].sendto = (int *) malloc(data_new.blocks[j].lines[i * fac + k].sendto_max * sizeof(int));
        data_new.blocks[j].lines[i * fac + k].frac = data->blocks[j].lines[i].frac;
        for (l = 0; l < data_new.blocks[j].lines[i * fac + k].recvfrom_max; l++) {
          data_new.blocks[j].lines[i * fac + k].recvfrom_node[l] = data->blocks[j].lines[i].recvfrom_node[l];
          data_new.blocks[j].lines[i * fac + k].recvfrom_line[l] = data->blocks[j].lines[i].recvfrom_line[l] * fac + k;
        }
        for (l = 0; l < data_new.blocks[j].lines[i * fac + k].sendto_max; l++) {
          data_new.blocks[j].lines[i * fac + k].sendto[l] = data->blocks[j].lines[i].sendto[l];
          if (data_new.blocks[j].lines[i * fac + k].sendto[l] >= 0) {
            data_new.blocks[j].lines[i * fac + k].sendto[l] = data_new.blocks[j].lines[i * fac + k].sendto[l];
          }
        }
      }
    }
  }
  ext_mpi_delete_algorithm(*data);
  *data = data_new;
}

int ext_mpi_generate_allreduce(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, ngroups[3], i, j, k,
      nbuffer_out_temp, nbuffer_in_temp, component, fac, fac_middle;
  struct parameters_block *parameters = NULL, *parameters_l = NULL;
  struct data_algorithm data, *data_l;
  char *buffer_in_temp, *buffer_out_temp;
  int num_ports[100], group[100];
  buffer_in_temp = (char *)malloc(1000000);
  buffer_out_temp = (char *)malloc(1000000);
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  ngroups[0] = get_ngroups(parameters, -1);
  ngroups[1] = get_ngroups(parameters, 0);
  ngroups[2] = get_ngroups(parameters, 1);
  data_l = (struct data_algorithm*)malloc(sizeof(struct data_algorithm)*(ngroups[0]+ngroups[1]+ngroups[2]));
  ext_mpi_read_parameters(buffer_in, &parameters_l);
  fac = fac_middle = 1;
  if (ngroups[1]) {
    nbuffer_out_temp = nbuffer_in_temp = 0;
    get_num_ports_group(parameters, 0, parameters_l->num_ports, parameters_l->groups);
    parameters_l->num_sockets = abs(parameters_l->groups[0]);
    fac = fac_middle = parameters_l->num_sockets;
    parameters_l->socket = socket_local(parameters_l->socket, 0, parameters->num_sockets / parameters_l->num_sockets, parameters_l->num_sockets, &component);
    nbuffer_in_temp += ext_mpi_write_parameters(parameters_l, buffer_in_temp + nbuffer_in_temp);
    nbuffer_in_temp += ext_mpi_write_eof(buffer_in_temp + nbuffer_in_temp, parameters_l->ascii_out);
    ext_mpi_generate_allreduce_single(buffer_in_temp, buffer_out_temp);
    ext_mpi_delete_parameters(parameters_l); parameters_l = NULL;
    nbuffer_out_temp += ext_mpi_read_parameters(buffer_out_temp + nbuffer_out_temp, &parameters_l);
    nbuffer_out_temp += ext_mpi_read_algorithm(buffer_out_temp + nbuffer_out_temp, &data_l[ngroups[0]], parameters_l->ascii_in);
    revise_partners(0, parameters->num_sockets, parameters_l->num_sockets, component, &data_l[ngroups[0]]);
  }
  for (i = 0; i < ngroups[0]; i++) {
    nbuffer_out_temp = nbuffer_in_temp = 0;
    get_num_ports_group(parameters, -i - 1, parameters_l->num_ports, parameters_l->groups);
    parameters_l->num_sockets = abs(parameters_l->groups[0]);
    parameters_l->socket = socket_local(parameters_l->socket, -i - 1, parameters->num_sockets / parameters_l->num_sockets, parameters_l->num_sockets, &component);
    nbuffer_in_temp += ext_mpi_write_parameters(parameters_l, buffer_in_temp + nbuffer_in_temp);
    nbuffer_in_temp += ext_mpi_write_eof(buffer_in_temp + nbuffer_in_temp, parameters_l->ascii_out);
    ext_mpi_generate_allreduce_single(buffer_in_temp, buffer_out_temp);
    ext_mpi_delete_parameters(parameters_l); parameters_l = NULL;
    nbuffer_out_temp += ext_mpi_read_parameters(buffer_out_temp + nbuffer_out_temp, &parameters_l);
    nbuffer_out_temp += ext_mpi_read_algorithm(buffer_out_temp + nbuffer_out_temp, &data_l[ngroups[0] - 1 - i], parameters_l->ascii_in);
    get_num_ports_group(parameters, -i - 1, num_ports, group);
    frac_multiply(fac, &data_l[ngroups[0] - 1 - i]);
    revise_partners(-1 - i, parameters->num_sockets, parameters_l->num_sockets, component, &data_l[ngroups[0] - 1 - i]);
    fac *= abs(group[0]);
  }
  fac = fac_middle;
  for (i = 0; i < ngroups[2]; i++) {
    nbuffer_out_temp = nbuffer_in_temp = 0;
    get_num_ports_group(parameters, i + 1, parameters_l->num_ports, parameters_l->groups);
    parameters_l->num_sockets = abs(parameters_l->groups[0]);
    parameters_l->socket = socket_local(parameters_l->socket, i + 1, parameters->num_sockets / parameters_l->num_sockets, parameters_l->num_sockets, &component);
    parameters_l->collective_type = collective_type_allgatherv;
    nbuffer_in_temp += ext_mpi_write_parameters(parameters_l, buffer_in_temp + nbuffer_in_temp);
    nbuffer_in_temp += ext_mpi_write_eof(buffer_in_temp + nbuffer_in_temp, parameters_l->ascii_out);
    ext_mpi_generate_allreduce_single(buffer_in_temp, buffer_out_temp);
    ext_mpi_delete_parameters(parameters_l); parameters_l = NULL;
    nbuffer_out_temp += ext_mpi_read_parameters(buffer_out_temp + nbuffer_out_temp, &parameters_l);
    nbuffer_out_temp += ext_mpi_read_algorithm(buffer_out_temp + nbuffer_out_temp, &data_l[ngroups[0] + ngroups[1] + i], parameters_l->ascii_in);
    get_num_ports_group(parameters, i + 1, num_ports, group);
    frac_multiply(fac, &data_l[ngroups[0] + ngroups[1] + i]);
    revise_partners(1 + i, parameters->num_sockets, parameters_l->num_sockets, component, &data_l[ngroups[0] + ngroups[1] + i]);
    fac *= abs(group[0]);
  }
  data.num_blocks = 0;
  for (i = 0; i < ngroups[0]+ngroups[1]+ngroups[2]; i++) {
    data.num_blocks += data_l[i].num_blocks;
  }
  data.blocks = (struct data_algorithm_block*)malloc(sizeof(struct data_algorithm_block)*data.num_blocks);
  for (i = k = 0; i < ngroups[0]+ngroups[1]+ngroups[2]; i++) {
    for (j = 0; j < data_l[i].num_blocks; j++) {
      data.blocks[k++] = data_l[i].blocks[j];
    }
  }
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_out += ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters_l);
  free(data.blocks);
  for (i = 0; i < ngroups[0]+ngroups[1]+ngroups[2]; i++) {
    ext_mpi_delete_algorithm(data_l[i]);
  }
  free(data_l);
  ext_mpi_delete_parameters(parameters);
  free(buffer_out_temp);
  free(buffer_in_temp);
  return nbuffer_out;
error:
//  ext_mpi_delete_algorithm(data_l);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
