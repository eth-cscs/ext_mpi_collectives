#include "allreduce_recursive.h"
#include "allreduce_bit.h"
#include "constants.h"
#include "read.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct allreduce_data_element {
  int max_lines;
  int *frac;
  int *source;
  int *num_from;
  int *from_value;
  int *from_line;
  int *num_to;
  int *to_value;
};

static int communication_partner(int gbstep, int num_ports, int task,
                                 int port) {
  return ((task / gbstep + port + 1) % (num_ports + 1) +
          (task / gbstep / (num_ports + 1)) * (num_ports + 1)) *
             gbstep +
         task % gbstep;
}

static int start_block(int gbstep, int num_ports, int task, int port) {
  return (task / gbstep) * gbstep;
}

static int end_block(int gbstep, int num_ports, int task, int port) {
  return (task / gbstep) * gbstep + gbstep;
}

static int allreduce_start(struct allreduce_data_element **stages,
                           int num_processors, int *num_ports, int task,
                           int allreduce) {
  int max_port_from, max_port_to;
  int nsteps, step, i;
  struct allreduce_data_element *stages_in = *stages;
  for (nsteps = 0; num_ports[nsteps]; nsteps++)
    ;
  *stages = (struct allreduce_data_element *)malloc(
      (nsteps + 1) * sizeof(struct allreduce_data_element));
  if (!(*stages))
    goto error;
  for (step = 0; step <= nsteps; step++) {
    (*stages)[step].frac = NULL;
    (*stages)[step].source = NULL;
    (*stages)[step].num_from = NULL;
    (*stages)[step].from_value = NULL;
    (*stages)[step].from_line = NULL;
    (*stages)[step].num_to = NULL;
    (*stages)[step].to_value = NULL;
  }
  if (!stages_in) {
    for (step = 0; step <= nsteps; step++) {
      (*stages)[step].max_lines = num_processors;
      if (step != 0) {
        max_port_from = abs(num_ports[step - 1]) + 1;
      } else {
        max_port_from = 1;
      }
      if (step != nsteps) {
        max_port_to = abs(num_ports[step]) + 1;
      } else {
        max_port_to = 2;
      }
      (*stages)[step].frac =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines);
      if (!(*stages)[step].frac)
        goto error;
      (*stages)[step].source =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines);
      if (!(*stages)[step].source)
        goto error;
      (*stages)[step].num_from =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines);
      if (!(*stages)[step].num_from)
        goto error;
      (*stages)[step].from_value = (int *)malloc(
          sizeof(int) * (*stages)[step].max_lines * max_port_from);
      if (!(*stages)[step].from_value)
        goto error;
      (*stages)[step].from_line = (int *)malloc(
          sizeof(int) * (*stages)[step].max_lines * max_port_from);
      if (!(*stages)[step].from_line)
        goto error;
      (*stages)[step].num_to =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines);
      if (!(*stages)[step].num_to)
        goto error;
      (*stages)[step].to_value =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines * max_port_to);
      if (!(*stages)[step].to_value)
        goto error;
      for (i = 0; i < (*stages)[0].max_lines; i++) {
        (*stages)[step].frac[i] = i;
        (*stages)[step].source[i] = task;
        (*stages)[step].num_from[i] = 0;
        (*stages)[step].num_to[i] = 0;
      }
    }
    if (allreduce) {
      for (i = 0; i < (*stages)[0].max_lines; i++) {
        (*stages)[0].num_from[i] = 1;
        (*stages)[0].from_value[i] = -1;
        (*stages)[0].from_line[i] = i;
      }
    } else {
      for (i = 0; i < (*stages)[0].max_lines; i++) {
        (*stages)[0].num_from[i] = 1;
        (*stages)[0].from_value[i] = task;
        (*stages)[0].from_line[i] = i;
      }
      for (i = start_block(1, 0, task, 0); i < end_block(1, 0, task, 0); i++) {
        (*stages)[0].from_value[i] = -1;
      }
    }
  }
  return 0;
error:
  if (*stages) {
    for (step = 0; step <= nsteps; step++) {
      free((*stages)[step].frac);
      free((*stages)[step].source);
      free((*stages)[step].num_from);
      free((*stages)[step].from_value);
      free((*stages)[step].from_line);
      free((*stages)[step].num_to);
      free((*stages)[step].to_value);
    }
  }
  free(*stages);
  return ERROR_MALLOC;
}

static void allreduce_done(struct allreduce_data_element *stages,
                           int *num_ports) {
  int nsteps, step;
  for (nsteps = 0; num_ports[nsteps]; nsteps++)
    ;
  for (step = 0; step <= nsteps; step++) {
    free(stages[step].to_value);
    free(stages[step].num_to);
    free(stages[step].from_line);
    free(stages[step].from_value);
    free(stages[step].num_from);
    free(stages[step].source);
    free(stages[step].frac);
  }
  free(stages);
}

static int allreduce_init(struct allreduce_data_element **stages,
                          int num_processors, int *num_ports, int task,
                          int allreduce) {
  int gbstep = INT_MAX, port, i, j, k, step, gbstep_prim, partner;
  if (allreduce_start(stages, num_processors, num_ports, task, allreduce) < 0)
    goto error;
  for (step = 0; num_ports[step] < 0; step++) {
    for (i = 0, gbstep = num_processors; (num_ports[i] < 0) && (i <= step);
         i++) {
      gbstep = (gbstep - 1) / (abs(num_ports[i]) + 1) + 1;
    }
    for (i = 0; i < (*stages)[step + 1].max_lines; i++) {
      (*stages)[step + 1].source[i] = (*stages)[step].source[i];
      (*stages)[step + 1].num_from[i] = 1;
      (*stages)[step + 1].from_value[i] = task;
      (*stages)[step + 1].from_line[i] = i;
    }
    for (port = 0; port < abs(num_ports[step]); port++) {
      partner = communication_partner(gbstep, abs(num_ports[step]), task, port);
      for (i = start_block(gbstep, abs(num_ports[step]), task, port);
           i < end_block(gbstep, abs(num_ports[step]), task, port); i++) {
        (*stages)[step + 1].from_value[i + (*stages)[step + 1].num_from[i] *
                                               (*stages)[step + 1].max_lines] =
            partner;
        (*stages)[step + 1].from_line[i + (*stages)[step + 1].num_from[i] *
                                              (*stages)[step + 1].max_lines] =
            i;
        (*stages)[step + 1].num_from[i]++;
      }
      for (i = start_block(gbstep, abs(num_ports[step]), partner, port);
           i < end_block(gbstep, abs(num_ports[step]), partner, port); i++) {
        (*stages)[step].to_value[i + (*stages)[step].num_to[i] *
                                         (*stages)[step].max_lines] = partner;
        (*stages)[step].num_to[i]++;
      }
    }
  }
  if (allreduce) {
    gbstep_prim = gbstep;
  } else {
    gbstep_prim = 0;
  }
  gbstep = 1;
  for (; num_ports[step] && (gbstep < gbstep_prim); step++) {
    for (i = 0; i < (*stages)[step + 1].max_lines; i++) {
      (*stages)[step + 1].source[i] = (*stages)[step].source[i];
      (*stages)[step + 1].num_from[i] = 1;
      (*stages)[step + 1].from_value[i] = task;
      (*stages)[step + 1].from_line[i] = i;
      k = 0;
      for (j = 0; j < (*stages)[step].num_from[i]; j++) {
        if ((*stages)[step].from_value[i + j * (*stages)[step].max_lines] !=
            task) {
          k = 1;
        }
      }
      if (k) {
        for (port = 0; port < abs(num_ports[step]); port++) {
          partner =
              communication_partner(gbstep, abs(num_ports[step]), task, port);
          (*stages)[step + 1]
              .from_value[i + (*stages)[step + 1].num_from[i] *
                                  (*stages)[step + 1].max_lines] = partner;
          (*stages)[step + 1].from_line[i + (*stages)[step + 1].num_from[i] *
                                                (*stages)[step + 1].max_lines] =
              i;
          (*stages)[step + 1].num_from[i]++;
          (*stages)[step].to_value[i + (*stages)[step].num_to[i] *
                                           (*stages)[step].max_lines] = partner;
          (*stages)[step].num_to[i]++;
        }
      }
    }
    gbstep *= abs(num_ports[step]) + 1;
  }
  for (; num_ports[step]; step++) {
    for (i = 0; i < (*stages)[step + 1].max_lines; i++) {
      (*stages)[step + 1].source[i] = (*stages)[step].source[i];
      (*stages)[step + 1].num_from[i] = 1;
      (*stages)[step + 1].from_value[i] = task;
      (*stages)[step + 1].from_line[i] = i;
    }
    for (port = 0; port < abs(num_ports[step]); port++) {
      partner = communication_partner(gbstep, abs(num_ports[step]), task, port);
      for (i = start_block(gbstep, abs(num_ports[step]), partner, port);
           i < end_block(gbstep, abs(num_ports[step]), partner, port); i++) {
        if ((*stages)[step + 1]
                .from_value[i + ((*stages)[step + 1].num_from[i] - 1) *
                                    (*stages)[step + 1].max_lines] == task) {
          (*stages)[step + 1].num_from[i]--;
        }
        (*stages)[step + 1].from_value[i + (*stages)[step + 1].num_from[i] *
                                               (*stages)[step + 1].max_lines] =
            partner;
        (*stages)[step + 1].from_line[i + (*stages)[step + 1].num_from[i] *
                                              (*stages)[step + 1].max_lines] =
            i;
        (*stages)[step + 1].num_from[i]++;
      }
      for (i = start_block(gbstep, abs(num_ports[step]), task, port);
           i < end_block(gbstep, abs(num_ports[step]), task, port); i++) {
        (*stages)[step].to_value[i + (*stages)[step].num_to[i] *
                                         (*stages)[step].max_lines] = partner;
        (*stages)[step].num_to[i]++;
      }
    }
    gbstep *= abs(num_ports[step]) + 1;
  }
  for (step = 0; num_ports[step]; step++) {
    for (i = 0; i < (*stages)[step].max_lines; i++) {
      if (!(*stages)[step].num_to[i]) {
        (*stages)[step].to_value[i + (*stages)[step].num_to[i] *
                                         (*stages)[step].max_lines] = task;
        (*stages)[step].num_to[i]++;
      }
    }
  }
  if (allreduce == 1) {
    for (i = 0; i < (*stages)[step].max_lines; i++) {
      k = 0;
      for (j = 0; j < (*stages)[step].num_from[i]; j++) {
        if ((*stages)[step].from_value[i + j * (*stages)[step].max_lines] !=
            task) {
          k = 1;
        }
      }
      if (k) {
        (*stages)[step].to_value[i + (*stages)[step].num_to[i] *
                                         (*stages)[step].max_lines] = -1;
        (*stages)[step].num_to[i]++;
      }
    }
  } else {
    for (i = 0; i < (*stages)[step].max_lines; i++) {
      if (!(*stages)[step].num_to[i]) {
        (*stages)[step].to_value[i + (*stages)[step].num_to[i] *
                                         (*stages)[step].max_lines] = -1;
        (*stages)[step].num_to[i]++;
      }
    }
  }
  return 0;
error:
  return ERROR_MALLOC;
}

int ext_mpi_generate_allreduce_recursive(char *buffer_in, char *buffer_out) {
  struct allreduce_data_element *stages = NULL;
  struct data_line **data = NULL;
  struct parameters_block *parameters;
  int i, j, l, nstep = 0;
  int task, num_nodes, size_level0 = 0, *size_level1 = NULL;
  int nbuffer_out = 0, nbuffer_in = 0, allreduce = -1;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  if (parameters->bit_identical) {
    i = ext_mpi_generate_allreduce_bit(buffer_in, buffer_out);
    ext_mpi_delete_parameters(parameters);
    return i;
  }
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  if (parameters->collective_type == collective_type_allgatherv) {
    allreduce = 0;
  }
  if (parameters->collective_type == collective_type_reduce_scatter) {
    allreduce = 1;
  }
  if (parameters->collective_type == collective_type_allreduce_group) {
    allreduce = 2;
  }
  if (parameters->collective_type == collective_type_allreduce_short) {
    allreduce = 3;
  }
  task = parameters->socket;
  num_nodes = parameters->num_sockets;
  for (i = 0; parameters->num_ports[i]; i++) {
    nstep++;
  }
  i = allreduce_init(&stages, num_nodes, parameters->num_ports, task,
                     allreduce);
  if (i < 0)
    goto error;
  size_level0 = nstep + 1;
  size_level1 = (int *)malloc(sizeof(int) * size_level0);
  if (!size_level1)
    goto error;
  data = (struct data_line **)malloc(sizeof(struct data_line *) * size_level0);
  if (!data)
    goto error;
  for (i = 0; i < size_level0; i++) {
    data[i] = NULL;
  }
  for (i = 0; i < size_level0; i++) {
    data[i] = NULL;
  }
  for (i = 0; i < size_level0; i++) {
    size_level1[i] = stages[i].max_lines;
    data[i] =
        (struct data_line *)malloc(sizeof(struct data_line) * size_level1[i]);
    if (!data[i])
      goto error;
    for (j = 0; j < size_level1[i]; j++) {
      data[i][j].to = NULL;
      data[i][j].from_node = NULL;
      data[i][j].from_line = NULL;
    }
    for (j = 0; j < stages[i].max_lines; j++) {
      data[i][j].frac = stages[i].frac[j];
      data[i][j].source = stages[i].source[j];
      data[i][j].to_max = stages[i].num_to[j];
      data[i][j].from_max = stages[i].num_from[j];
      data[i][j].to = (int *)malloc(sizeof(int) * data[i][j].to_max);
      if (!data[i][j].to)
        goto error;
      data[i][j].from_node = (int *)malloc(sizeof(int) * data[i][j].from_max);
      if (!data[i][j].from_node)
        goto error;
      data[i][j].from_line = (int *)malloc(sizeof(int) * data[i][j].from_max);
      if (!data[i][j].from_line)
        goto error;
      for (l = 0; l < data[i][j].to_max; l++) {
        data[i][j].to[l] = stages[i].to_value[l * stages[i].max_lines + j];
      }
      for (l = 0; l < data[i][j].from_max; l++) {
        data[i][j].from_node[l] =
            stages[i].from_value[l * stages[i].max_lines + j];
        data[i][j].from_line[l] =
            stages[i].from_line[l * stages[i].max_lines + j];
      }
    }
  }
  nbuffer_out +=
      ext_mpi_write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                              parameters->ascii_out);
  ext_mpi_delete_algorithm(size_level0, size_level1, data);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  allreduce_done(stages, parameters->num_ports);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(size_level0, size_level1, data);
  allreduce_done(stages, parameters->num_ports);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
