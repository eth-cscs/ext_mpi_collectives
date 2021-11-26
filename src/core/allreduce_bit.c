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

static int allreduce_start(struct allreduce_data_element **stages,
                           int num_processors, int *num_ports, int task,
                           int allreduce) {
  int max_port_from, max_port_to;
  int nsteps, step, min_lines, i = -1;
  struct allreduce_data_element *stages_in = *stages;
  for (nsteps = 0; num_ports[nsteps]; nsteps++) {
  }
  *stages = (struct allreduce_data_element *)malloc(
      (nsteps + 1) * sizeof(struct allreduce_data_element));
  if (!*stages)
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
      if (allreduce) {
        if (allreduce == 3) {
          for ((*stages)[step].max_lines = 1, i = 1;
               (num_ports[i - 1] > 0) && (i <= step);
               (*stages)[step].max_lines =
                   ((*stages)[step].max_lines - 1) / (num_ports[i - 1] + 1) + 1,
              i++) {
          }
        } else {
          for ((*stages)[step].max_lines = num_processors, i = 1;
               (num_ports[i - 1] > 0) && (i <= step);
               (*stages)[step].max_lines =
                   ((*stages)[step].max_lines - 1) / (num_ports[i - 1] + 1) + 1,
              i++) {
          }
        }
      } else {
        (*stages)[step].max_lines = 1;
        i = 1;
      }
      min_lines = (*stages)[step].max_lines;
      for (; num_ports[i - 1] && (i <= step);
           (*stages)[step].max_lines *= (abs(num_ports[i - 1]) + 1), i++) {
      }
      if ((*stages)[step].max_lines > num_processors * min_lines) {
        (*stages)[step].max_lines = num_processors * min_lines;
      }
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
      (*stages)[step].from_value =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines *
                        max_port_from * num_processors);
      if (!(*stages)[step].from_value)
        goto error;
      (*stages)[step].from_line =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines *
                        max_port_from * num_processors);
      if (!(*stages)[step].from_line)
        goto error;
      (*stages)[step].num_to =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines);
      if (!(*stages)[step].num_to)
        goto error;
      (*stages)[step].to_value = (int *)malloc(
          sizeof(int) * (*stages)[step].max_lines * (max_port_to + 1));
      if (!(*stages)[step].to_value)
        goto error;
    }
    for (i = 0; i < (*stages)[0].max_lines; i++) {
      if (allreduce == 3) {
        (*stages)[0].frac[i] = 0;
      } else {
        (*stages)[0].frac[i] = (i + task) % num_processors;
      }
      (*stages)[0].source[i] = task;
      (*stages)[0].num_from[i] = 1;
      (*stages)[0].from_value[i] = -1;
      (*stages)[0].from_line[i] = i;
    }
  } else {
    for (step = 0; step <= nsteps; step++) {
      if (allreduce) {
        if (allreduce == 3) {
          for ((*stages)[step].max_lines = 1, i = 1;
               (num_ports[i - 1] > 0) && (i <= step);
               (*stages)[step].max_lines =
                   ((*stages)[step].max_lines - 1) / (num_ports[i - 1] + 1) + 1,
              i++) {
          }
        } else {
          for ((*stages)[step].max_lines = num_processors, i = 1;
               (num_ports[i - 1] > 0) && (i <= step);
               (*stages)[step].max_lines =
                   ((*stages)[step].max_lines - 1) / (num_ports[i - 1] + 1) + 1,
              i++) {
          }
        }
      } else {
        (*stages)[step].max_lines = 1;
        i = 1;
      }
      min_lines = (*stages)[step].max_lines;
      for (; num_ports[i - 1] && (i <= step);
           (*stages)[step].max_lines *= (abs(num_ports[i - 1]) + 1), i++) {
      }
      if ((*stages)[step].max_lines > num_processors * min_lines) {
        (*stages)[step].max_lines = num_processors * min_lines;
      }
      if (allreduce) {
        (*stages)[step].max_lines *= stages_in[0].max_lines / num_processors;
      } else {
        (*stages)[step].max_lines *= stages_in[0].max_lines;
      }
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
      (*stages)[step].from_value =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines *
                        max_port_from * num_processors);
      if (!(*stages)[step].from_value)
        goto error;
      (*stages)[step].from_line =
          (int *)malloc(sizeof(int) * (*stages)[step].max_lines *
                        max_port_from * num_processors);
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
    }
    for (i = 0; i < (*stages)[0].max_lines; i++) {
      (*stages)[0].frac[i] = stages_in[0].frac[i];
      (*stages)[0].source[i] = stages_in[0].source[i];
      (*stages)[0].num_from[i] = 1;
      (*stages)[0].from_value[i] = -1;
      (*stages)[0].from_line[i] = i;
    }
  }
  return 0;
error:
  if (stages) {
    for (step = 0; step <= nsteps; step++) {
      free((*stages)[i].frac);
      free((*stages)[i].source);
      free((*stages)[i].num_from);
      free((*stages)[i].from_value);
      free((*stages)[i].from_line);
      free((*stages)[i].num_to);
      free((*stages)[i].to_value);
    }
  }
  free(*stages);
  return ERROR_MALLOC;
}

static void allreduce_done(struct allreduce_data_element *stages,
                           int *num_ports) {
  int nsteps, step;
  for (nsteps = 0; num_ports[nsteps]; nsteps++) {
  }
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
  int chunk, gbstep, port, i, j, k, l, m, step, task_dup, step_mid, frac_max;
  i = allreduce_start(stages, num_processors, num_ports, task, allreduce);
  if (i < 0)
    goto error;
  for (step = 0; num_ports[step] > 0; step++) {
    for (i = 0, gbstep = num_processors; (num_ports[i] > 0) && (i <= step);
         i++) {
      gbstep = (gbstep - 1) / (num_ports[i] + 1) + 1;
    }
    for (i = 0; i < gbstep * (*stages)[0].max_lines / num_processors; i++) {
      (*stages)[step + 1].frac[i] = (*stages)[step].frac[i];
      (*stages)[step + 1].source[i] = (*stages)[step].source[i];
      (*stages)[step + 1].num_from[i] = 1;
      (*stages)[step + 1].from_value[i] = task;
      (*stages)[step + 1].from_line[i] = -1;
    }
    for (port = 0; port < num_ports[step]; port++) {
      for (i = 0;
           (i < gbstep * (*stages)[0].max_lines / num_processors) &&
           ((port + 1) * gbstep * (*stages)[0].max_lines / num_processors + i <
            (*stages)[step].max_lines);
           i++) {
        (*stages)[step + 1].num_from[i] = 1;
        (*stages)[step + 1].from_value[i] = task;
        (*stages)[step + 1].from_line[i] = i;
      }
    }
    for (port = 0; port < num_ports[step]; port++) {
      task_dup = (num_processors + task - (port + 1) * gbstep) % num_processors;
      for (i = 0;
           (i < gbstep * (*stages)[0].max_lines / num_processors) &&
           ((port + 1) * gbstep * (*stages)[0].max_lines / num_processors + i <
            (*stages)[step].max_lines);
           i++) {
        (*stages)[step + 1].frac[i] =
            ((*stages)[0].max_lines +
             (*stages)[step].frac[gbstep * (port + 1) * (*stages)[0].max_lines /
                                      num_processors +
                                  i] +
             (task_dup - task) * (*stages)[0].max_lines / num_processors) %
            (*stages)[0].max_lines;
        (*stages)[step + 1].source[i] = task;
        (*stages)[step + 1].from_value[(*stages)[step + 1].max_lines *
                                           (*stages)[step + 1].num_from[i] +
                                       i] = task_dup;
        (*stages)[step + 1].from_line[(*stages)[step + 1].max_lines *
                                          (*stages)[step + 1].num_from[i] +
                                      i] = (port + 1) * gbstep + i;
        (*stages)[step + 1].num_from[i]++;
      }
    }
  }
  step_mid = step;
  for (step = step_mid; num_ports[step]; step++) {
    chunk = (*stages)[step].max_lines;
    for (i = 0; num_ports[i] > 0; i++) {
    }
    gbstep = 1;
    while ((num_ports[i] != 0) && (i < step)) {
      gbstep *= abs(num_ports[i]) + 1;
      i++;
    }
    for (i = 0; i < chunk; i++) {
      (*stages)[step + 1].frac[i] = (*stages)[step].frac[i];
      (*stages)[step + 1].source[i] = (*stages)[step].source[i];
      (*stages)[step + 1].num_from[i] = 1;
      (*stages)[step + 1].from_value[i] = task;
      (*stages)[step + 1].from_line[i] = i;
    }
    for (port = 0; port < abs(num_ports[step]); port++) {
      task_dup = (num_processors + task + (port + 1) * gbstep) % num_processors;
      for (i = 0; (i < chunk) &&
                  (chunk * (port + 1) + i < (*stages)[step + 1].max_lines);
           i++) {
        (*stages)[step + 1].num_from[chunk * (port + 1) + i] = 1;
        (*stages)[step + 1].from_value[chunk * (port + 1) + i] =
            (num_processors + task_dup) % num_processors;
        //        (*stages)[step+1].from_line[chunk*(port+1)+i] =
        //        chunk*(port+1)+i;
        (*stages)[step + 1].from_line[chunk * (port + 1) + i] =
            chunk * port + i;
        (*stages)[step + 1].frac[chunk * (port + 1) + i] =
            ((*stages)[0].max_lines + (*stages)[step].frac[i] +
             (task_dup - task) * (*stages)[0].max_lines / num_processors) %
            (*stages)[0].max_lines;
        if (!allreduce) {
          (*stages)[step + 1].frac[chunk * (port + 1) + i] =
              (num_processors * (*stages)[0].max_lines +
               (*stages)[step].frac[i] +
               (task_dup - task) * (*stages)[0].max_lines) %
              (num_processors * (*stages)[0].max_lines);
        }
        (*stages)[step + 1].source[chunk * (port + 1) + i] =
            (num_processors + (*stages)[step].source[i] + task_dup - task) %
            num_processors;
      }
    }
  }
  if (allreduce != 2) {
    gbstep = num_processors;
    for (step = 0; num_ports[step] > 0; step++) {
      chunk = ((*stages)[step].max_lines - 1) / (num_ports[step] + 1) + 1;
      gbstep = (gbstep - 1) / (num_ports[step] + 1) + 1;
      for (i = 0; i < chunk; i++) {
        (*stages)[step].num_to[i] = 1;
        (*stages)[step].to_value[i] = task;
      }
      for (port = 0; port < num_ports[step]; port++) {
        for (i = 0; (i < chunk) &&
                    (i + (port + 1) * chunk < (*stages)[step].max_lines);
             i++) {
          (*stages)[step].num_to[i + (port + 1) * chunk] = 1;
          (*stages)[step].to_value[i + (port + 1) * chunk] =
              (num_processors + task + (port + 1) * gbstep) % num_processors;
        }
      }
    }
  } else {
    gbstep = num_processors;
    for (step = 0; num_ports[step] > 0; step++) {
      chunk = (((*stages)[step].max_lines /
                    ((*stages)[0].max_lines / num_processors) -
                1) /
                   (num_ports[step] + 1) +
               1) *
              ((*stages)[0].max_lines / num_processors);
      gbstep = (gbstep - 1) / (num_ports[step] + 1) + 1;
      for (i = 0; i < chunk; i++) {
        (*stages)[step].num_to[i] = 1;
        (*stages)[step].to_value[i] = task;
      }
      for (port = 0; port < num_ports[step]; port++) {
        for (i = 0; (i < chunk) &&
                    (i + (port + 1) * chunk < (*stages)[step].max_lines);
             i++) {
          (*stages)[step].num_to[i + (port + 1) * chunk] = 1;
          (*stages)[step].to_value[i + (port + 1) * chunk] =
              (num_processors + task + (port + 1) * gbstep) % num_processors;
        }
      }
    }
  }
  gbstep = 1;
  for (; num_ports[step]; step++) {
    for (i = 0; i < (*stages)[step].max_lines; i++) {
      (*stages)[step].num_to[i] = 1;
      (*stages)[step].to_value[i] = task;
      for (port = 0; port < abs(num_ports[step]); port++) {
        if ((port + 1) * (*stages)[step].max_lines + i <
            (*stages)[step + 1].max_lines) {
          (*stages)[step]
              .to_value[(*stages)[step].max_lines * (*stages)[step].num_to[i] +
                        i] =
              (num_processors + task - (port + 1) * gbstep) % num_processors;
          (*stages)[step].num_to[i]++;
        }
      }
    }
    gbstep *= (abs(num_ports[step]) + 1);
  }
  for (i = 0; i < (*stages)[step].max_lines; i++) {
    (*stages)[step].num_to[i] = 1;
    (*stages)[step].to_value[i] = task;
  }
  frac_max = -1;
  for (i = 0; i < (*stages)[step].max_lines; i++) {
    if ((*stages)[step].frac[i] > frac_max) {
      frac_max = (*stages)[step].frac[i];
    }
  }
  for (k = 0; k <= frac_max; k++) {
    l = INT_MAX - 1;
    m = -1;
    for (i = 0; i < (*stages)[step].max_lines; i++) {
      if (((*stages)[step].frac[i] == k) && ((*stages)[step].source[i] < l)) {
        l = (*stages)[step].source[i];
        m = i;
      }
    }
    if (m >= 0) {
      (*stages)[step]
          .to_value[(*stages)[step].num_to[m] * (*stages)[step].max_lines + m] =
          -1;
      (*stages)[step].num_to[m]++;
    }
    for (j = l + 1; j < num_processors; j++) {
      for (i = 0; i < (*stages)[step].max_lines; i++) {
        if (((*stages)[step].frac[i] == k) &&
            ((*stages)[step].source[i] == j)) {
          (*stages)[step].from_value[(*stages)[step].max_lines *
                                         (*stages)[step].num_from[m] +
                                     m] = task;
          (*stages)[step].from_line[(*stages)[step].max_lines *
                                        (*stages)[step].num_from[m] +
                                    m] = i;
          (*stages)[step].num_from[m]++;
        }
      }
    }
  }
  return 0;
error:
  return ERROR_MALLOC;
}

int ext_mpi_generate_allreduce_bit(char *buffer_in, char *buffer_out) {
  struct allreduce_data_element *stages = NULL, *stages_in = NULL;
  struct data_line **data = NULL;
  struct parameters_block *parameters;
  int i, j, k, l, nstep, max_lines;
  int task, num_nodes, *num_ports = NULL, size_level0 = 0, *size_level1 = NULL;
  int nbuffer_out = 0, nbuffer_in = 0, allreduce = 1;
  nbuffer_in += i = read_parameters(buffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  if (parameters->collective_type == collective_type_allgatherv) {
    allreduce = 0;
  }
  if (parameters->collective_type == collective_type_allreduce_group) {
    allreduce = 2;
  }
  if (parameters->collective_type == collective_type_allreduce_short) {
    allreduce = 3;
  }
  task = parameters->node;
  num_nodes = parameters->num_nodes;
  for (i=0; parameters->num_ports[i]; i++);
  num_ports = (int*)malloc((i+1)*sizeof(int));
  if (!num_ports) goto error;
  for (i=0; parameters->num_ports[i]; i++){
    num_ports[i] = -parameters->num_ports[i];
  }
  num_ports[i] = 0;
  i = read_algorithm(buffer_in + nbuffer_in, &size_level0, &size_level1, &data,
                     parameters->ascii_in);
  if (i < 0)
    goto error;
  for (nstep = 0; num_ports[nstep]; nstep++)
    ;
  if (size_level0) {
    stages = (struct allreduce_data_element *)malloc(
        sizeof(struct allreduce_data_element) * (nstep + 1));
    if (!stages)
      goto error;
    for (i = 0; i <= nstep; i++) {
      stages[i].frac = NULL;
      stages[i].source = NULL;
      stages[i].num_from = NULL;
      stages[i].from_value = NULL;
      stages[i].from_line = NULL;
      stages[i].num_to = NULL;
      stages[i].to_value = NULL;
    }
    stages_in = stages;
    stages[0].max_lines = size_level1[0];
    stages[0].frac = (int *)malloc(sizeof(int) * stages[0].max_lines);
    if (!stages[0].frac)
      goto error;
    stages[0].source = (int *)malloc(sizeof(int) * stages[0].max_lines);
    if (!stages[0].source)
      goto error;
    stages[0].num_from = (int *)malloc(sizeof(int) * stages[0].max_lines);
    if (!stages[0].num_from)
      goto error;
    stages[0].from_value = (int *)malloc(sizeof(int) * stages[0].max_lines * 2);
    if (!stages[0].from_value)
      goto error;
    stages[0].from_line = (int *)malloc(sizeof(int) * stages[0].max_lines * 2);
    if (!stages[0].from_line)
      goto error;
    stages[0].num_to = (int *)malloc(sizeof(int) * stages[0].max_lines);
    if (!stages[0].num_to)
      goto error;
    stages[0].to_value = (int *)malloc(sizeof(int) * stages[0].max_lines * 2);
    if (!stages[0].to_value)
      goto error;
    for (i = 0; i < size_level1[0]; i++) {
      stages[0].frac[i] = data[0][i].frac;
      stages[0].source[i] = data[0][i].source;
      stages[0].num_from[i] = 1;
      stages[0].from_value[i] = -1;
      stages[0].from_line[i] = -1;
    }
    for (i = 0, max_lines = stages[0].max_lines; i <= nstep; i++) {
      if (num_ports[i] < 0) {
        max_lines += max_lines * (abs(num_ports[i]) + 1);
      }
    }
  } else {
    for (i = 0, max_lines = num_nodes; i <= nstep; i++) {
      if (num_ports[i] < 0) {
        max_lines += max_lines * (abs(num_ports[i]) + 1);
      }
    }
  }
  i = allreduce_init(&stages, num_nodes, num_ports, task, allreduce);
  if (i < 0)
    return i;
  if (size_level0) {
    free(stages_in[0].to_value);
    free(stages_in[0].num_to);
    free(stages_in[0].from_line);
    free(stages_in[0].from_value);
    free(stages_in[0].num_from);
    free(stages_in[0].source);
    free(stages_in[0].frac);
    free(stages_in);
  }
  delete_algorithm(size_level0, size_level1, data);
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
    size_level1[i] = 0;
    for (j = 0; j < stages[i].max_lines; j++) {
      size_level1[i]++;
    }
    data[i] =
        (struct data_line *)malloc(sizeof(struct data_line) * size_level1[i]);
    if (!data[i])
      goto error;
    for (j = 0; j < size_level1[i]; j++) {
      data[i][j].to = NULL;
      data[i][j].from_node = NULL;
      data[i][j].from_line = NULL;
    }
    k = 0;
    for (j = 0; j < stages[i].max_lines; j++) {
      data[i][k].frac = stages[i].frac[j];
      data[i][k].source = stages[i].source[j];
      data[i][k].to_max = stages[i].num_to[j];
      data[i][k].from_max = stages[i].num_from[j];
      data[i][k].to = (int *)malloc(sizeof(int) * data[i][k].to_max);
      if (!data[i][k].to)
        goto error;
      data[i][k].from_node = (int *)malloc(sizeof(int) * data[i][k].from_max);
      if (!data[i][k].from_node)
        goto error;
      data[i][k].from_line = (int *)malloc(sizeof(int) * data[i][k].from_max);
      if (!data[i][k].from_line)
        goto error;
      for (l = 0; l < data[i][k].to_max; l++) {
        data[i][k].to[l] = stages[i].to_value[l * stages[i].max_lines + j];
      }
      for (l = 0; l < data[i][k].from_max; l++) {
        data[i][k].from_node[l] =
            stages[i].from_value[l * stages[i].max_lines + j];
        data[i][k].from_line[l] =
            stages[i].from_line[l * stages[i].max_lines + j];
      }
      k++;
    }
  }
  nbuffer_out +=
      write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                      parameters->ascii_out);
  delete_algorithm(size_level0, size_level1, data);
  nbuffer_out += write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  allreduce_done(stages, num_ports);
  free(num_ports);
  delete_parameters(parameters);
  return nbuffer_out;
error:
  delete_algorithm(size_level0, size_level1, data);
  allreduce_done(stages, num_ports);
  free(num_ports);
  delete_parameters(parameters);
  return ERROR_MALLOC;
}
