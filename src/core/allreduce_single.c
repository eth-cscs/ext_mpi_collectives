#include "allreduce_single.h"
#include "constants.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*static int update_lines_used(struct allreduce_data_element *stages,
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
}*/

static int allreduce_init(int *size_level0, int **size_level1, struct data_line ***data,
                          int num_sockets, int *num_ports, int task, int allreduce) {
  int gbstep, i, j, k, l, m, step, task_dup, step_mid, flag, line, line2, *size_level1b,
      *used = NULL, used_max;
  for (step = 0; num_ports[step]; step++);
  *size_level0 = 0;
  *size_level1 = (int*)malloc(sizeof(int)*(2*step+1));
  size_level1b = (int*)malloc(sizeof(int)*(2*step+1));
  *data = (struct data_line**)malloc(sizeof(struct data_line*)*(2*step+1));
  (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*num_sockets*abs(num_ports[0]));
  size_level1b[*size_level0] = (*size_level1)[*size_level0] = num_sockets;
  for (line = 0; line < size_level1b[*size_level0]; line++) {
    (*data)[*size_level0][line].frac = (line + task) % num_sockets;
    (*data)[*size_level0][line].recvfrom_max = 1;
    (*data)[*size_level0][line].recvfrom_node = (int*)malloc(sizeof(int)*(*data)[*size_level0][line].recvfrom_max);
    (*data)[*size_level0][line].recvfrom_line = (int*)malloc(sizeof(int)*(*data)[*size_level0][line].recvfrom_max);
    (*data)[*size_level0][line].recvfrom_node[0] = -1;
    (*data)[*size_level0][line].recvfrom_line[0] = line;
    (*data)[*size_level0][line].sendto_max = 0;
    (*data)[*size_level0][line].sendto = NULL;
    (*data)[*size_level0][line].reducefrom_max = 0;
    (*data)[*size_level0][line].reducefrom = NULL;
  }
  for (step = 0, gbstep = 1; num_ports[step] < 0; gbstep*=abs(num_ports[step]) + 1, step++) {
    if (step >= 1) {
      (*size_level0)++;
      (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*size_level1b[*size_level0 - 1]*abs(num_ports[step]));
      size_level1b[*size_level0] = (*size_level1)[*size_level0] = (size_level1b[*size_level0 - 1]  - 1) / (abs(num_ports[step - 1]) + 1) + 1;
      for (line = 0; line < size_level1b[*size_level0]; line++) {
        (*data)[*size_level0][line].frac = (*data)[*size_level0 - 1][line].frac;
        (*data)[*size_level0][line].sendto_max = 0;
        (*data)[*size_level0][line].sendto = NULL;
        (*data)[*size_level0][line].recvfrom_max = 0;
        (*data)[*size_level0][line].recvfrom_node = NULL;
        (*data)[*size_level0][line].recvfrom_line = NULL;
        (*data)[*size_level0][line].reducefrom_max = 0;
        (*data)[*size_level0][line].reducefrom = NULL;
      }
    }
    for (i = line = 0; i < abs(num_ports[step]) + 1; i++) {
      j = size_level1b[*size_level0] / (abs(num_ports[step]) + 1);
      if (i < size_level1b[*size_level0] % (abs(num_ports[step]) + 1)) j++;
      for (l = 0; l < j; l++) {
        if (i > 0) {
          (*data)[*size_level0][line].sendto_max = 1;
          (*data)[*size_level0][line].sendto = (int*)malloc(sizeof(int)*(*data)[*size_level0][line].sendto_max);
          (*data)[*size_level0][line].sendto[0] = (task + gbstep * i) % num_sockets;
          (*data)[*size_level0][(*size_level1)[*size_level0]].frac = (*data)[*size_level0][l].frac;
          (*data)[*size_level0][(*size_level1)[*size_level0]].recvfrom_max = 1;
          (*data)[*size_level0][(*size_level1)[*size_level0]].recvfrom_node = (int*)malloc(sizeof(int)*(*data)[*size_level0][size_level1b[*size_level0]].recvfrom_max);
          (*data)[*size_level0][(*size_level1)[*size_level0]].recvfrom_line = (int*)malloc(sizeof(int)*(*data)[*size_level0][size_level1b[*size_level0]].recvfrom_max);
          (*data)[*size_level0][(*size_level1)[*size_level0]].recvfrom_node[0] = (num_sockets + task - gbstep * i) % num_sockets;
          (*data)[*size_level0][(*size_level1)[*size_level0]].recvfrom_line[0] = line;
          (*data)[*size_level0][(*size_level1)[*size_level0]].sendto_max = 0;
          (*data)[*size_level0][(*size_level1)[*size_level0]].sendto = NULL;
          (*data)[*size_level0][(*size_level1)[*size_level0]].reducefrom_max = 0;
          (*data)[*size_level0][(*size_level1)[*size_level0]].reducefrom = NULL;
          (*size_level1)[*size_level0]++;
        }
        line++;
      }
    }
    (*size_level0)++;
    (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*(*size_level1)[*size_level0 - 1]);
    (*size_level1)[*size_level0] = (*size_level1)[*size_level0 - 1];
    size_level1b[*size_level0] = size_level1b[*size_level0 - 1];
    for (line = 0; line < (*size_level1)[*size_level0]; line++) {
      (*data)[*size_level0][line].frac = (*data)[*size_level0 - 1][line].frac;
      (*data)[*size_level0][line].sendto_max = 0;
      (*data)[*size_level0][line].sendto = NULL;
      (*data)[*size_level0][line].recvfrom_max = 0;
      (*data)[*size_level0][line].recvfrom_node = NULL;
      (*data)[*size_level0][line].recvfrom_line = NULL;
      (*data)[*size_level0][line].reducefrom_max = 0;
      (*data)[*size_level0][line].reducefrom = (int*)malloc(sizeof(int)*abs(num_ports[step]));
    }
    for (line = 0; line < size_level1b[*size_level0]; line++) {
      for (line2 = size_level1b[*size_level0]; line2 < (*size_level1)[*size_level0]; line2++) {
        if ((*data)[*size_level0][line].frac == (*data)[*size_level0][line2].frac) {
          (*data)[*size_level0][line].reducefrom[(*data)[*size_level0][line].reducefrom_max++] = line2;
        }
      }
    }
  }
  (*size_level0)++;
  free(size_level1b);
  return 0;
error:
  free(used);
  return ERROR_MALLOC;
}

int ext_mpi_generate_allreduce_single(char *buffer_in, char *buffer_out) {
  struct data_line **data = NULL;
  struct parameters_block *parameters;
  int i, j, k, l, m, nstep;
  int size_level0 = 0, *size_level1 = NULL;
  int nbuffer_out = 0, nbuffer_in = 0, allreduce = 1;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  if (parameters->collective_type == collective_type_reduce_scatter) {
    allreduce = 2;
  }
  if (parameters->collective_type == collective_type_allgatherv) {
    allreduce = 0;
  }
  if (parameters->collective_type == collective_type_allreduce_group) {
    allreduce = 2;
  }
  if (parameters->collective_type == collective_type_allreduce_short) {
    allreduce = 3;
  }
  i = allreduce_init(&size_level0, &size_level1, &data, parameters->num_sockets, parameters->num_ports, parameters->socket, allreduce);
  if (i < 0)
    goto error;
  nbuffer_out +=
      ext_mpi_write_algorithm(size_level0, size_level1, data, buffer_out + nbuffer_out,
                              parameters->ascii_out);
  ext_mpi_delete_algorithm(size_level0, size_level1, data);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(size_level0, size_level1, data);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
