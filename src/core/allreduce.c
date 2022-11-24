#include "allreduce.h"
#include "allreduce_single.h"
#include "constants.h"
#include "read_write.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int get_ngroups(struct parameters_block *parameters, int dire) {
  int ngroups = 0, group, ports, fac = 1;
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
      for (group = ports - 1; group >= 0; group--) {
        if (parameters->groups[group] < 0) {
          fac *= abs(parameters->groups[group]);
        }
      }
      for (ports = 0; parameters->num_ports[ports]; ports++)
        ;
      for (group = ports - 1; group >= 0; group--) {
        if (parameters->groups[group] < 0) {
          fac /= abs(parameters->groups[group]);
          if (fac >= 1) {
            ngroups++;
          } else {
            break;
          }
        }
      }
      break;
  }
  return ngroups;
}

static int get_num_ports_group(struct parameters_block *parameters, int step, int *num_ports, int *group) {
  int ngroups, ports_group, length = 0, i;
  if (parameters->num_ports[0] == 0) {
    return 0;
  }
  if (step == 0) {
    if (get_ngroups(parameters, 0) <= 0) {
      num_ports[0] = INT_MAX;
      group[0] = -1;
      num_ports[1] = group[1] = 0;
      return 0;
    }
    for (ngroups = get_ngroups(parameters, -1), ports_group = 0; ngroups > 0; ngroups--) {
      for (; parameters->groups[ports_group] > 0; ports_group++)
        ;
      ports_group++;
    }
  } else if (step < 0) {
    ngroups = get_ngroups(parameters, -1);
    if (-step > ngroups) {
      return 0;
    }
    for (i = ports_group = 0; i < ngroups + step; i++) {
      for (; parameters->groups[ports_group] > 0; ports_group++)
        ;
      ports_group++;
    }
  } else {
    ngroups = get_ngroups(parameters, 1);
    if (step > ngroups) {
      return 0;
    }
    for (ports_group = 0; parameters->groups[ports_group]; ports_group++)
      ;
    for (i = 0, ports_group--; i <= ngroups - step; i++) {
      ports_group--;
      for (; parameters->groups[ports_group] > 0; ports_group--)
        ;
    }
    ports_group++;
  }
  for (; parameters->groups[ports_group] > 0; ports_group++, length++) {
    num_ports[length] = parameters->num_ports[ports_group];
    group[length] = parameters->groups[ports_group];
  }
  num_ports[length] = parameters->num_ports[ports_group];
  group[length++] = parameters->groups[ports_group];
  num_ports[length] = group[length] = 0;
  return length;
}

static int socket_local(struct parameters_block *parameters, int socket_global, int step, int *components) {
  int num_ports[100], group[100], i, j;
  if (socket_global >= 0) {
    j = parameters->num_sockets;
    for (i = 0; i <= abs(step); i++) {
      if (step <= 0) {
        get_num_ports_group(parameters, -i, num_ports, group);
      } else {
        get_num_ports_group(parameters, i, num_ports, group);
      }
      j /= abs(group[0]);
    }
    components[0] = socket_global % j;
    components[1] = (socket_global / j) / abs(group[0]);
    return (socket_global / j) % abs(group[0]);
  } else {
    return socket_global;
  }
}

static int socket_global(struct parameters_block *parameters, int socket_local, int step, int *components) {
  int num_ports[100], group[100], i, j;
  if (socket_local >= 0) {
    j = parameters->num_sockets;
    for (i = 0; i <= abs(step); i++) {
      if (step <= 0) {
        get_num_ports_group(parameters, -i, num_ports, group);
      } else {
        get_num_ports_group(parameters, i, num_ports, group);
      }
      j /= abs(group[0]);
    }
    socket_local += abs(group[0]) * components[1];
    return socket_local * j + components[0];
  } else {
    return socket_local;
  }
}

static void revise_partners(struct parameters_block *parameters, int step, int *components, struct data_algorithm *data) {
  int i, j, k;
  for (i = 0; i < data->num_blocks; i++) {
    for (j = 0; j < data->blocks[i].num_lines; j++) {
      for (k = 0; k < data->blocks[i].lines[j].sendto_max; k++) {
        data->blocks[i].lines[j].sendto_node[k] = socket_global(parameters, data->blocks[i].lines[j].sendto_node[k], step, components);
      }
      for (k = 0; k < data->blocks[i].lines[j].recvfrom_max; k++) {
        data->blocks[i].lines[j].recvfrom_node[k] = socket_global(parameters, data->blocks[i].lines[j].recvfrom_node[k], step, components);
      }
      if (step == 0) {
        data->blocks[i].lines[j].frac = socket_global(parameters, data->blocks[i].lines[j].frac, step, components);
      }
    }
  }
}

static int get_num_fracs(struct parameters_block *parameters, int step) {
  int ret = 1, i;
  int num_ports[100], group[100];
  for (i = 0; i <= abs(step); i++) {
    if (step <= 0) {
      get_num_ports_group(parameters, step + i, num_ports, group);
    } else {
      get_num_ports_group(parameters, step - i, num_ports, group);
    }
    ret *= abs(group[0]);
  }
  return ret;
}

static int expand_frac(struct parameters_block *parameters, int socket, int step, int *fracs) {
  int components[2], num_frac, num_fracs, i, socket_l;
  int num_ports[100], group[100];
  get_num_ports_group(parameters, step, num_ports, group);
  if (step == 0) {
    for (num_frac = 0; num_frac < abs(group[0]); num_frac++) {
      fracs[num_frac] = (((num_frac + (socket / (parameters->num_sockets / abs(group[0]))) * abs(group[0])) % abs(group[0])) * (parameters->num_sockets / abs(group[0])) + socket) % parameters->num_sockets;
    }
  } else {
    num_fracs = get_num_fracs(parameters, step) / abs(group[0]);
    socket_l = socket_local(parameters, socket, step, components);
    num_frac = 0;
    for (i = 0; i < abs(group[0]); i++) {
      if (step < 0) {
        expand_frac(parameters, socket_global(parameters, (i + socket_l) % abs(group[0]), step, components), step + 1, fracs + num_frac);
      } else {
        expand_frac(parameters, socket_global(parameters, (i + socket_l) % abs(group[0]), step, components), step - 1, fracs + num_frac);
      }
      num_frac += num_fracs;
    }
  }
  return num_frac;
}

static void frac_multiply(int fac, int *fracs, struct data_algorithm *data){
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
        data_new.blocks[j].lines[i * fac + k].sendto_node = (int *) malloc(data_new.blocks[j].lines[i * fac + k].sendto_max * sizeof(int));
        data_new.blocks[j].lines[i * fac + k].sendto_line = (int *) malloc(data_new.blocks[j].lines[i * fac + k].sendto_max * sizeof(int));
        data_new.blocks[j].lines[i * fac + k].reducefrom_max = data->blocks[j].lines[i].reducefrom_max;
        data_new.blocks[j].lines[i * fac + k].reducefrom = (int *) malloc(data_new.blocks[j].lines[i * fac + k].reducefrom_max * sizeof(int));
        for (l = 0; l < data_new.blocks[j].lines[i * fac + k].recvfrom_max; l++) {
          data_new.blocks[j].lines[i * fac + k].recvfrom_node[l] = data->blocks[j].lines[i].recvfrom_node[l];
          data_new.blocks[j].lines[i * fac + k].recvfrom_line[l] = data->blocks[j].lines[i].recvfrom_line[l] * fac + k;
        }
        for (l = 0; l < data_new.blocks[j].lines[i * fac + k].sendto_max; l++) {
          data_new.blocks[j].lines[i * fac + k].sendto_node[l] = data->blocks[j].lines[i].sendto_node[l];
          data_new.blocks[j].lines[i * fac + k].sendto_line[l] = data->blocks[j].lines[i].sendto_line[l] + k;
        }
        for (l = 0; l < data_new.blocks[j].lines[i * fac + k].reducefrom_max; l++) {
          data_new.blocks[j].lines[i * fac + k].reducefrom[l] = data->blocks[j].lines[i].reducefrom[l] * fac + k;
        }
        data_new.blocks[j].lines[i * fac + k].frac = fracs[data->blocks[j].lines[i].frac * fac + k];
      }
    }
  }
  ext_mpi_delete_algorithm(*data);
  *data = data_new;
}

static void revise_frac(int num_sockets, struct data_algorithm *data) {
  int i, j;
  for (j = data->num_blocks - 1; j >= 0; j--) {
    for (i = data->blocks[j].num_lines - 1; i >= 0; i--) {
      data->blocks[j].lines[i].frac = (num_sockets + data->blocks[j].lines[i].frac - data->blocks[0].lines[0].frac) % num_sockets;
    }
  }
}

static int chancel_copyin_copyout(struct data_algorithm *data) {
  int i, j, flag, ret = 1;
  for (i = 1; i < data->num_blocks - 1; i++) {
    flag = 1;
    for (j = (data->blocks[i].num_lines < data->blocks[i + 1].num_lines ? data->blocks[i].num_lines : data->blocks[i + 1].num_lines) - 1; j >=0 ; j--) {
      if ((data->blocks[i].lines[j].sendto_max > 0 && data->blocks[i].lines[j].sendto_node[0] == -1) !=
          (data->blocks[i + 1].lines[j].recvfrom_max > 0 && data->blocks[i + 1].lines[j].recvfrom_node[0] == -1)) {
        ret = flag = 0;
      } else if ((data->blocks[i].lines[j].sendto_max > 0 && data->blocks[i].lines[j].sendto_node[0] == -1) &&
                 (data->blocks[i].lines[j].frac != data->blocks[i + 1].lines[j].frac)) {
        ret = flag = 0;
      }
    }
    if (flag) {
      for (j = 0; j < data->blocks[i].num_lines; j++) {
        if (data->blocks[i].lines[j].sendto_max > 0 && data->blocks[i].lines[j].sendto_node[0] == -1) {
          free(data->blocks[i].lines[j].sendto_node);
          free(data->blocks[i].lines[j].sendto_line);
          data->blocks[i].lines[j].sendto_node = data->blocks[i].lines[j].sendto_line = NULL;
          data->blocks[i].lines[j].sendto_max = 0;
        }
        if (data->blocks[i].lines[j].recvfrom_max > 0 && data->blocks[i].lines[j].recvfrom_node[0] == -1) {
          free(data->blocks[i].lines[j].recvfrom_node);
          free(data->blocks[i].lines[j].recvfrom_line);
          data->blocks[i].lines[j].recvfrom_node = NULL;
          data->blocks[i].lines[j].recvfrom_line = NULL;
          data->blocks[i].lines[j].recvfrom_max = 0;
        }
      }
    } else {
      i++;
    }
  }
  return ret;
}

static void add_lines(struct data_algorithm *data) {
  struct data_algorithm_line *lines_new;
  int block = -1, i, j, k;
  for (i = 1; block < 0 && i < data->num_blocks - 1; i++) {
    for (j = 0; block < 0 && j < data->blocks[i].num_lines; j++) {
      if (data->blocks[i].lines[j].sendto_max > 0 && data->blocks[i].lines[j].sendto_node[0] == -1) {
        block = i;
      }
    }
  }
  for (i = block + 1; i < data->num_blocks; i++) {
    lines_new = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line) * (data->blocks[i].num_lines + data->blocks[block].num_lines));
    memset(lines_new, 0, sizeof(struct data_algorithm_line) * data->blocks[block].num_lines);
    memcpy(lines_new + data->blocks[block].num_lines, data->blocks[i].lines, data->blocks[i].num_lines * sizeof(struct data_algorithm_line));
    for (j = 0; j < data->blocks[block].num_lines; j++) {
      lines_new[j].frac = data->blocks[block].lines[j].frac;
    }
    for (j = 0; j < data->blocks[i].num_lines; j++) {
      for (k = 0; k < lines_new[j + data->blocks[block].num_lines].recvfrom_max; k++) {
        lines_new[j + data->blocks[block].num_lines].recvfrom_line[k] += data->blocks[block].num_lines;
      }
      for (k = 0; k < lines_new[j + data->blocks[block].num_lines].reducefrom_max; k++) {
        lines_new[j + data->blocks[block].num_lines].reducefrom[k] += data->blocks[block].num_lines;
      }
      for (k = 0; k < lines_new[j + data->blocks[block].num_lines].copyreducefrom_max; k++) {
        lines_new[j + data->blocks[block].num_lines].copyreducefrom[k] += data->blocks[block].num_lines;
      }
    }
    data->blocks[i].num_lines += data->blocks[block].num_lines;
    free(data->blocks[i].lines);
    data->blocks[i].lines = lines_new;
  }
  for (i = 0; i < data->blocks[block].num_lines; i++) {
    if (data->blocks[block].lines[i].sendto_max > 0 && data->blocks[block].lines[i].sendto_node[0] == -1) {
      for (j = 0; j < data->blocks[block + 1].num_lines; j++) {
        if (data->blocks[block + 1].lines[j].recvfrom_max > 0 && data->blocks[block + 1].lines[j].recvfrom_node[0] == -1) {
          if (data->blocks[block].lines[i].frac == data->blocks[block + 1].lines[j].frac) {
            free(data->blocks[block].lines[i].sendto_node);
            free(data->blocks[block].lines[i].sendto_line);
            data->blocks[block].lines[i].sendto_node = data->blocks[block].lines[i].sendto_line = NULL;
            data->blocks[block].lines[i].sendto_max = 0;
            free(data->blocks[block + 1].lines[j].recvfrom_node);
            data->blocks[block + 1].lines[j].recvfrom_node = NULL;
            free(data->blocks[block + 1].lines[j].recvfrom_line);
            data->blocks[block + 1].lines[j].recvfrom_line = NULL;
            data->blocks[block + 1].lines[j].recvfrom_max = 0;
            data->blocks[block + 1].lines[j].copyreducefrom_max = 1;
	    data->blocks[block + 1].lines[j].copyreducefrom = (int*)malloc(sizeof(int));
            data->blocks[block + 1].lines[j].copyreducefrom[0] = i;
          }
        }
      }
    }
  }
}

int ext_mpi_generate_allreduce(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, ngroups[3], i, j, k, *fracs,
      nbuffer_out_temp, nbuffer_in_temp, components[2], fac, fac_middle;
  struct parameters_block *parameters = NULL, *parameters_l = NULL;
  struct data_algorithm data, *data_l;
  char *buffer_in_temp, *buffer_out_temp;
  int num_ports[100], group[100];
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  ngroups[0] = get_ngroups(parameters, -1);
  ngroups[1] = get_ngroups(parameters, 0);
  ngroups[2] = get_ngroups(parameters, 1);
  if (ngroups[0]+ngroups[1]+ngroups[2] == 1) {
    ext_mpi_delete_parameters(parameters);
    return ext_mpi_generate_allreduce_single(buffer_in, buffer_out);
  }
  buffer_in_temp = (char *)malloc(1000000);
  buffer_out_temp = (char *)malloc(1000000);
  fracs = (int *)malloc(sizeof(int) * parameters->num_sockets);
  data_l = (struct data_algorithm*)malloc(sizeof(struct data_algorithm)*(ngroups[0]+ngroups[1]+ngroups[2]));
  ext_mpi_read_parameters(buffer_in, &parameters_l);
  fac = fac_middle = 1;
  if (ngroups[1]) {
    nbuffer_out_temp = nbuffer_in_temp = 0;
    get_num_ports_group(parameters, 0, parameters_l->num_ports, parameters_l->groups);
    parameters_l->num_sockets = abs(parameters_l->groups[0]);
    fac = fac_middle = parameters_l->num_sockets;
    parameters_l->socket = socket_local(parameters, parameters->socket, 0, components);
    nbuffer_in_temp += ext_mpi_write_parameters(parameters_l, buffer_in_temp + nbuffer_in_temp);
    nbuffer_in_temp += ext_mpi_write_eof(buffer_in_temp + nbuffer_in_temp, parameters_l->ascii_out);
    ext_mpi_generate_allreduce_single(buffer_in_temp, buffer_out_temp);
    ext_mpi_delete_parameters(parameters_l); parameters_l = NULL;
    nbuffer_out_temp += ext_mpi_read_parameters(buffer_out_temp + nbuffer_out_temp, &parameters_l);
    nbuffer_out_temp += ext_mpi_read_algorithm(buffer_out_temp + nbuffer_out_temp, &data_l[ngroups[0]], parameters_l->ascii_in);
    revise_partners(parameters, 0, components, &data_l[ngroups[0]]);
  }
  for (i = 0; i < ngroups[0]; i++) {
    nbuffer_out_temp = nbuffer_in_temp = 0;
    get_num_ports_group(parameters, -i - 1, parameters_l->num_ports, parameters_l->groups);
    parameters_l->num_sockets = abs(parameters_l->groups[0]);
    parameters_l->socket = socket_local(parameters, parameters->socket, -i - 1, components);
    nbuffer_in_temp += ext_mpi_write_parameters(parameters_l, buffer_in_temp + nbuffer_in_temp);
    nbuffer_in_temp += ext_mpi_write_eof(buffer_in_temp + nbuffer_in_temp, parameters_l->ascii_out);
    ext_mpi_generate_allreduce_single(buffer_in_temp, buffer_out_temp);
    ext_mpi_delete_parameters(parameters_l); parameters_l = NULL;
    nbuffer_out_temp += ext_mpi_read_parameters(buffer_out_temp + nbuffer_out_temp, &parameters_l);
    nbuffer_out_temp += ext_mpi_read_algorithm(buffer_out_temp + nbuffer_out_temp, &data_l[ngroups[0] - 1 - i], parameters_l->ascii_in);
    get_num_ports_group(parameters, -i - 1, num_ports, group);
    expand_frac(parameters, parameters->socket, -i - 1, fracs);
    revise_frac(abs(group[0]), &data_l[ngroups[0] - 1 - i]);
    frac_multiply(fac, fracs, &data_l[ngroups[0] - 1 - i]);
    revise_partners(parameters, -i - 1, components, &data_l[ngroups[0] - 1 - i]);
    fac *= abs(group[0]);
  }
  fac = fac_middle;
  for (i = 0; i < ngroups[2]; i++) {
    nbuffer_out_temp = nbuffer_in_temp = 0;
    get_num_ports_group(parameters, i + 1, parameters_l->num_ports, parameters_l->groups);
    parameters_l->num_sockets = abs(parameters_l->groups[0]);
    parameters_l->socket = socket_local(parameters, parameters->socket, i + 1, components);
    parameters_l->collective_type = collective_type_allgatherv;
    nbuffer_in_temp += ext_mpi_write_parameters(parameters_l, buffer_in_temp + nbuffer_in_temp);
    nbuffer_in_temp += ext_mpi_write_eof(buffer_in_temp + nbuffer_in_temp, parameters_l->ascii_out);
    ext_mpi_generate_allreduce_single(buffer_in_temp, buffer_out_temp);
    ext_mpi_delete_parameters(parameters_l); parameters_l = NULL;
    nbuffer_out_temp += ext_mpi_read_parameters(buffer_out_temp + nbuffer_out_temp, &parameters_l);
    nbuffer_out_temp += ext_mpi_read_algorithm(buffer_out_temp + nbuffer_out_temp, &data_l[ngroups[0] + ngroups[1] + i], parameters_l->ascii_in);
    get_num_ports_group(parameters, i + 1, num_ports, group);
    expand_frac(parameters, parameters->socket, i + 1, fracs);
    revise_frac(abs(group[0]), &data_l[ngroups[0] + ngroups[1] + i]);
    frac_multiply(fac, fracs, &data_l[ngroups[0] + ngroups[1] + i]);
    revise_partners(parameters, i + 1, components, &data_l[ngroups[0] + ngroups[1] + i]);
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
  if (!chancel_copyin_copyout(&data)) {
    add_lines(&data);
  }
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_out += ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters_l);
  for (i = 0; i < ngroups[0]+ngroups[1]+ngroups[2]; i++) {
    free(data_l[i].blocks);
  }
  free(data_l);
  ext_mpi_delete_algorithm(data);
  free(fracs);
  ext_mpi_delete_parameters(parameters);
  free(buffer_out_temp);
  free(buffer_in_temp);
  return nbuffer_out;
error:
//  ext_mpi_delete_algorithm(data_l);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
