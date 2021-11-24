#include "allreduce.h"
#include "allreduce_bit.h"
#include "constants.h"
#include "read.h"
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
  int nsteps, step, min_lines, i;
  struct allreduce_data_element *stages_in = *stages;
  for (nsteps = 0; num_ports[nsteps]; nsteps++) {
  }
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

static int update_lines_used(struct allreduce_data_element *stages,
                             int *num_ports, int *lines_used) {
  int step, port, i, flag;
  flag = 0;
  for (step = 0; num_ports[step]; step++) {
  }
  for (step--; (step >= 0) && (num_ports[step] < 0); step--) {
    for (port = 0; port < abs(num_ports[step]); port++) {
      for (i = 0; (i < stages[step].max_lines) &&
                  (i + (port + 1) * stages[step].max_lines <
                   stages[step + 1].max_lines);
           i++) {
        if (lines_used[(port + 1) * stages[step].max_lines + i]) {
          if (!lines_used[i]) {
            flag = 1;
          }
          lines_used[i] = 1;
        }
      }
    }
  }
  return flag;
}

static int allreduce_init(struct allreduce_data_element **stages,
                          int num_processors, int *num_ports, int *lines_used,
                          int task, int allreduce) {
  int chunk, gbstep, port, i, j, k, l, m, step, task_dup, step_mid, flag, im,
      *used = NULL, used_max;
  i = allreduce_start(stages, num_processors, num_ports, task, allreduce);
  if (i < 0)
    goto error;
  used_max = (*stages)[0].max_lines;
  for (step = 0; num_ports[step]; step++) {
    if ((*stages)[step + 1].max_lines > used_max) {
      used_max = (*stages)[step + 1].max_lines;
    }
  }
  used = (int *)malloc(used_max * sizeof(int));
  if (!used)
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
    for (port = 0; port < abs(num_ports[step]); port++) {
      im = chunk;
      if (chunk * (port + 1) + im > (*stages)[step + 1].max_lines - 1) {
        im = (*stages)[step + 1].max_lines - chunk * (port + 1);
      }
      for (i = 0; i < im; i++) {
        for (j = 0; j < used_max; j++) {
          used[j] = 1;
        }
        for (j = (port + 1) * chunk - 1;
             (j >= 0) && used[(*stages)[step + 1].frac[j]]; j--) {
          if ((*stages)[step + 1].frac[j] ==
              (*stages)[step + 1].frac[chunk * (port + 1) + i]) {
            (*stages)[step + 1].from_value
                [(*stages)[step + 1].max_lines *
                     (*stages)[step + 1].num_from[chunk * (port + 1) + i] +
                 chunk * (port + 1) + i] = task;
            (*stages)[step + 1].from_line
                [(*stages)[step + 1].max_lines *
                     (*stages)[step + 1].num_from[chunk * (port + 1) + i] +
                 chunk * (port + 1) + i] = j;
            (*stages)[step + 1].num_from[chunk * (port + 1) + i]++;
            used[(*stages)[step + 1].frac[j]] = 0;
          }
        }
      }
    }
    for (i = 0; i < used_max; i++) {
      used[i] = 1;
    }
    for (i = (*stages)[step + 1].max_lines - 1; i >= 0; i--) {
      if (used[(*stages)[step + 1].frac[i]]) {
        lines_used[i] = 1;
        used[(*stages)[step + 1].frac[i]] = 0;
      }
    }
  }
  flag = 1;
  while (flag) {
    flag = 0;
    for (step = step_mid + 1; num_ports[step - 1]; step++) {
      for (i = 0; i < (*stages)[step].max_lines; i++) {
        for (k = 0; k < (*stages)[step].num_from[i]; k++) {
          m = (*stages)[step].from_line[(*stages)[step].max_lines * k + i];
          if ((k > 0) && (m >= 0) &&
              ((*stages)[step].from_value[(*stages)[step].max_lines * k + i] ==
               task)) {
            if (!lines_used[m]) {
              flag = 1;
              lines_used[m] = 1;
            }
          }
        }
      }
    }
    if (flag) {
      flag = update_lines_used(*stages, num_ports, lines_used);
    }
  }
  for (step = step_mid + 1; num_ports[step - 1]; step++) {
    chunk = (*stages)[step - 1].max_lines;
    for (port = 0; port < abs(num_ports[step - 1]); port++) {
      for (i = 0;
           (i < chunk) && (chunk * (port + 1) + i < (*stages)[step].max_lines);
           i++) {
        for (k = 0; k < (*stages)[step].num_from[chunk * (port + 1) + i]; k++) {
          if ((*stages)[step].from_line[(*stages)[step].max_lines * k +
                                        chunk * (port + 1) + i] >= 0) {
            j = l = (*stages)[step].from_line[(*stages)[step].max_lines * k +
                                              chunk * (port + 1) + i];
            for (m = 0; m < j; m++) {
              if (!lines_used[m]) {
                l--;
              }
            }
            (*stages)[step].from_line[(*stages)[step].max_lines * k +
                                      chunk * (port + 1) + i] = l;
          }
        }
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
          if (lines_used[(port + 1) * (*stages)[step].max_lines + i]) {
            (*stages)[step].to_value[(*stages)[step].max_lines *
                                         (*stages)[step].num_to[i] +
                                     i] =
                (num_processors + task - (port + 1) * gbstep) % num_processors;
            (*stages)[step].num_to[i]++;
          }
        }
      }
    }
    gbstep *= (abs(num_ports[step]) + 1);
  }
  for (i = 0; i < used_max; i++) {
    used[i] = 1;
  }
  for (i = (*stages)[step].max_lines - 1; i >= 0; i--) {
    (*stages)[step].num_to[i] = 1;
    (*stages)[step].to_value[i] = task;
    if (used[(*stages)[step].frac[i]]) {
      (*stages)[step].to_value[(*stages)[step].max_lines + i] = -1;
      (*stages)[step].num_to[i]++;
      used[(*stages)[step].frac[i]] = 0;
    }
  }
  free(used);
  return 0;
error:
  free(used);
  return ERROR_MALLOC;
}

int ext_mpi_generate_allreduce(char *buffer_in, char *buffer_out) {
  struct allreduce_data_element *stages = NULL, *stages_in = NULL;
  struct data_line **data = NULL;
  struct parameters_block *parameters;
  int i, j, k, l, *lines_used = NULL, nstep, max_lines;
  int task, num_nodes, *num_ports = NULL, size_level0 = 0, *size_level1 = NULL;
  int nbuffer_out = 0, nbuffer_in = 0, allreduce = 1;
  nbuffer_in += i = read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  if (parameters->bit_identical) {
    i = ext_mpi_generate_allreduce_bit(buffer_in, buffer_out);
    delete_parameters(parameters);
    return i;
  }
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
  num_ports = parameters->num_ports;
  i = read_algorithm(buffer_in + nbuffer_in, &size_level0, &size_level1, &data,
                     parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  for (nstep = 0; num_ports[nstep]; nstep++)
    ;
  if (size_level0) {
    stages = (struct allreduce_data_element *)malloc(
        sizeof(struct allreduce_data_element) * (nstep + 1));
    if (!stages)
      goto error;
    for (i = 0; i <= nstep; i++) {
      stages[i].to_value = NULL;
      stages[i].num_to = NULL;
      stages[i].from_line = NULL;
      stages[i].from_value = NULL;
      stages[i].num_from = NULL;
      stages[i].source = NULL;
      stages[i].frac = NULL;
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
  lines_used = (int *)malloc(max_lines * sizeof(int));
  if (!lines_used)
    return ERROR_MALLOC;
  if (allreduce) {
    for (i = 0; i < max_lines; i++) {
      lines_used[i] = 0;
    }
  } else {
    for (i = 0; i < max_lines; i++) {
      lines_used[i] = 1;
    }
  }
  i = allreduce_init(&stages, num_nodes, num_ports, lines_used, task,
                     allreduce);
  if (i < 0)
    goto error;
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
      if (lines_used[j] || (i == 0) || (num_ports[i - 1] > 0)) {
        size_level1[i]++;
      }
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
      if (lines_used[j] || (i == 0) || (num_ports[i - 1] > 0)) {
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
  }
  for (i = 0; i < size_level0; i++) {
    for (j = 0; j < size_level1[i]; j++) {
      for (k = 0; k < data[i][j].from_max; k++) {
        if ((data[i][j].from_node[k] == task) &&
            (data[i][j].from_line[k] > j)) {
          data[i][j].from_line[k] = j;
        }
      }
    }
  }
  nbuffer_out +=
      write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                      parameters->ascii_out);
  delete_algorithm(size_level0, size_level1, data);
  nbuffer_out += write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  allreduce_done(stages, num_ports);
  free(lines_used);
  delete_parameters(parameters);
  return nbuffer_out;
error:
  delete_algorithm(size_level0, size_level1, data);
  allreduce_done(stages, num_ports);
  free(lines_used);
  delete_parameters(parameters);
  return ERROR_MALLOC;
}
