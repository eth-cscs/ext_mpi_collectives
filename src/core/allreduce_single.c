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

static int get_size_level1b(int num_sockets, int *num_ports, int step, int flag) {
  int ret = 1, min = num_sockets, i;
  if (flag) {
    for (i = 0; i < step; i++) {
      if (num_ports[i] < 0) {
        min = (min - 1) / (-num_ports[i] + 1) + 1;
      } else if (num_ports[i]) {
        ret *= num_ports[i + 1] + 1;
      }
    }
    if (num_ports[step] > 0) {
      for (step = 0; num_ports[step] < 0; step++);
      ret *= num_ports[step] + 1;
    }
    if (ret > num_sockets) {
      ret = num_sockets;
    }
    return min * ret;
  } else {
    for (i = 0; i < step; i++) {
      if (num_ports[i] < 0) {
        min = (min - 1) / (-num_ports[i] + 1) + 1;
      } else if (num_ports[i]) {
        ret *= num_ports[i] + 1;
      }
    }
    return min * ret;
  }
}

static int allgather_core(int *size_level0, int **size_level1, struct data_line ***data,
                          int num_sockets, int *num_ports, int task) {
  int gbstep, i, l, step, line;
  for (step = 0; num_ports[step]; step++);
  *size_level0 = 0;
  *size_level1 = (int*)malloc(sizeof(int)*(2*step+2));
  *data = (struct data_line**)malloc(sizeof(struct data_line*)*(2*step+2));
  (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*num_sockets*abs(num_ports[0]));
  (*size_level1)[*size_level0] = 1;
  (*data)[*size_level0][0].frac = task;
  (*data)[*size_level0][0].recvfrom_max = 1;
  (*data)[*size_level0][0].recvfrom_node = (int*)malloc(sizeof(int)*(*data)[*size_level0][0].recvfrom_max);
  (*data)[*size_level0][0].recvfrom_line = (int*)malloc(sizeof(int)*(*data)[*size_level0][0].recvfrom_max);
  (*data)[*size_level0][0].recvfrom_node[0] = -1;
  (*data)[*size_level0][0].recvfrom_line[0] = 0;
  (*data)[*size_level0][0].sendto_max = 0;
  (*data)[*size_level0][0].sendto = NULL;
  (*data)[*size_level0][0].reducefrom_max = 0;
  (*data)[*size_level0][0].reducefrom = NULL;
  (*size_level0)++;
  for (step = 0, gbstep = 1; num_ports[step]; gbstep *= num_ports[step] + 1, step++) {
    (*size_level1)[*size_level0] = (*size_level1)[*size_level0 - 1] * (num_ports[step] + 1);
    if ((*size_level1)[*size_level0] > num_sockets) (*size_level1)[*size_level0] = num_sockets;
    (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*(*size_level1)[*size_level0]);
    for (line = 0; line < (*size_level1)[*size_level0]; line++) {
      l = gbstep;
      if (l > (*size_level1)[*size_level0] - gbstep) l = (*size_level1)[*size_level0] - gbstep;
      if (line < l) {
        (*data)[*size_level0][line].sendto_max = num_ports[step];
        if (line >= (*size_level1)[*size_level0] - num_ports[step] * (*size_level1)[*size_level0 - 1]) (*data)[*size_level0][line].sendto_max--;
        (*data)[*size_level0][line].sendto = (int*)malloc(sizeof(int)*(*data)[*size_level0][line].sendto_max);
        for (i = 0; i < (*data)[*size_level0][line].sendto_max; i++) {
          (*data)[*size_level0][line].sendto[i] = (task + num_sockets - (i + 1) * gbstep) % num_sockets;
        }
        (*data)[*size_level0][line].recvfrom_max = 0;
        (*data)[*size_level0][line].recvfrom_node = NULL;
        (*data)[*size_level0][line].recvfrom_line = NULL;
      } else if (line >= gbstep) {
        (*data)[*size_level0][line].sendto_max = 0;
        (*data)[*size_level0][line].sendto = NULL;
        (*data)[*size_level0][line].recvfrom_max = 1;
        (*data)[*size_level0][line].recvfrom_node = (int *)malloc(sizeof(int)*(*data)[*size_level0][line].recvfrom_max);
        (*data)[*size_level0][line].recvfrom_line = (int *)malloc(sizeof(int)*(*data)[*size_level0][line].recvfrom_max);
        (*data)[*size_level0][line].recvfrom_node[0] = (task + num_sockets + (line / gbstep) * gbstep) % num_sockets;
        (*data)[*size_level0][line].recvfrom_line[0] = line % gbstep;
      } else {
        (*data)[*size_level0][line].sendto_max = 0;
        (*data)[*size_level0][line].sendto = NULL;
        (*data)[*size_level0][line].recvfrom_max = 0;
        (*data)[*size_level0][line].recvfrom_node = NULL;
        (*data)[*size_level0][line].recvfrom_line = NULL;
      }
      (*data)[*size_level0][line].frac = (task + line) % num_sockets;
      (*data)[*size_level0][line].reducefrom_max = 0;
      (*data)[*size_level0][line].reducefrom = NULL;
    }
    (*size_level0)++;
  }
  (*size_level1)[*size_level0] = (*size_level1)[*size_level0 - 1];
  (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*(*size_level1)[*size_level0]);
  for (line = 0; line < (*size_level1)[*size_level0]; line++) {
    (*data)[*size_level0][line].frac = (*data)[*size_level0 - 1][line].frac;
    (*data)[*size_level0][line].sendto_max = 1;
    (*data)[*size_level0][line].sendto = (int*)malloc(sizeof(int)*(*data)[*size_level0][line].sendto_max);
    (*data)[*size_level0][line].sendto[0] = -1;
    (*data)[*size_level0][line].recvfrom_max = 0;
    (*data)[*size_level0][line].recvfrom_node = NULL;
    (*data)[*size_level0][line].recvfrom_line = NULL;
    (*data)[*size_level0][line].reducefrom_max = 0;
    (*data)[*size_level0][line].reducefrom = NULL;
  }
  (*size_level0)++;
  return 0;
}

static int allreduce_core(int *size_level0, int **size_level1, struct data_line ***data,
                          int num_sockets, int *num_ports, int task) {
  int gbstep, i, j, k, l, m, step, line, line2, *size_level1b;
  for (step = 0; num_ports[step]; step++);
  *size_level0 = 0;
  *size_level1 = (int*)malloc(sizeof(int)*(2*step+2));
  if (!*size_level1) goto error;
  size_level1b = (int*)malloc(sizeof(int)*(2*step+2));
  if (!size_level1b) goto error;
  *data = (struct data_line**)malloc(sizeof(struct data_line*)*(2*step+2));
  (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*num_sockets*(abs(num_ports[0]) + 1));
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
  (*size_level0)++;
  for (step = 0, gbstep = (num_sockets - 1) / (-num_ports[step] + 1) + 1; num_ports[step] < 0; gbstep = (gbstep - 1) / (-num_ports[step] + 1) + 1, step++) {
    size_level1b[*size_level0] = (*size_level1)[*size_level0] = get_size_level1b(num_sockets, num_ports, step, 1);
    (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*size_level1b[*size_level0 - 1]*(-num_ports[step] + 1));
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
    (*size_level0)++;
  }
  if (num_ports[step]) {
    for (gbstep = 1; num_ports[step]; gbstep *= num_ports[step] + 1, step++) {
      size_level1b[*size_level0] = (*size_level1)[*size_level0] = get_size_level1b(num_sockets, num_ports, step, 1);
      (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*size_level1b[*size_level0]);
      l = size_level1b[*size_level0] - get_size_level1b(num_sockets, num_ports, step, 0);
      i = l / num_ports[step];
      j = l % num_ports[step];
      for (line = 0; line < size_level1b[*size_level0]; line++) {
        if (line < get_size_level1b(num_sockets, num_ports, step, 0) && line >= l) {
          (*data)[*size_level0][line].frac = -1;
        } else if (line < get_size_level1b(num_sockets, num_ports, step, 0)) {
          (*data)[*size_level0][line].frac = task;
        } else if (line >= (size_level1b[*size_level0] - 1) / (num_ports[step] + 1) + 1) {
          for (k = 0; k < num_ports[step]; k++) {
            for (m = 0; m < (k < j ? i + 1 : i) && line < size_level1b[*size_level0]; m++) {
              (*data)[*size_level0][line].frac = -10 - (k + 1);
              line++;
            }
          }
        } else {
          printf("logical error in allreduce_core\n");
          exit(1);
        }
      }
      for (line = size_level1b[*size_level0] - 1; line >= 0; line--) {
        if ((*data)[*size_level0][line].frac == task) {
          (*data)[*size_level0][line].sendto_max = num_ports[step];
          (*data)[*size_level0][line].sendto = (int*)malloc(sizeof(int)*(*data)[*size_level0][line].sendto_max);
          for (i = 0; i < (*data)[*size_level0][line].sendto_max; i++) {
            (*data)[*size_level0][line].sendto[i] = (task + (i + 1) * gbstep) % num_sockets;
          }
          (*data)[*size_level0][line].recvfrom_max = 0;
          (*data)[*size_level0][line].recvfrom_node = NULL;
          (*data)[*size_level0][line].recvfrom_line = NULL;
          (*data)[*size_level0][line].frac = (*data)[*size_level0 - 1][line].frac;
        } else if ((*data)[*size_level0][line].frac <= -10) {
          for (j = 0; (*data)[*size_level0][line - j].frac == (*data)[*size_level0][line].frac; j++)
            ;
          j--;
          (*data)[*size_level0][line].frac = -(*data)[*size_level0][line].frac - 10;
          (*data)[*size_level0][line].sendto_max = 0;
          (*data)[*size_level0][line].sendto = NULL;
          (*data)[*size_level0][line].recvfrom_max = 1;
          (*data)[*size_level0][line].recvfrom_node = (int *)malloc(sizeof(int)*(*data)[*size_level0][line].recvfrom_max);
          (*data)[*size_level0][line].recvfrom_line = (int *)malloc(sizeof(int)*(*data)[*size_level0][line].recvfrom_max);
          (*data)[*size_level0][line].recvfrom_node[0] = (task + num_sockets - (*data)[*size_level0][line].frac * gbstep) % num_sockets;
          (*data)[*size_level0][line].recvfrom_line[0] = j;
          (*data)[*size_level0][line].frac = ((*data)[*size_level0 - 1][j].frac + num_sockets + (*data)[*size_level0][line].frac * gbstep) % num_sockets;
        } else {
          (*data)[*size_level0][line].frac = (*data)[*size_level0 - 1][line].frac;
          (*data)[*size_level0][line].sendto_max = 0;
          (*data)[*size_level0][line].sendto = NULL;
          (*data)[*size_level0][line].recvfrom_max = 0;
          (*data)[*size_level0][line].recvfrom_node = NULL;
          (*data)[*size_level0][line].recvfrom_line = NULL;
        }
        (*data)[*size_level0][line].reducefrom_max = 0;
        (*data)[*size_level0][line].reducefrom = NULL;
      }
      (*size_level0)++;
    }
    size_level1b[*size_level0] = (*size_level1)[*size_level0] = (*size_level1)[*size_level0 - 1];
    (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*size_level1b[*size_level0]);
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
    for (i = 0; i < num_sockets; i++){
      for (line = 0; line < size_level1b[*size_level0]; line++) {
        if ((*data)[*size_level0][line].frac == i) {
          for (j = line + 1; j < size_level1b[*size_level0]; j++) {
            if ((*data)[*size_level0][j].frac == i) {
              (*data)[*size_level0][line].reducefrom_max++;
            }
          }
          (*data)[*size_level0][line].reducefrom = (int*)malloc(sizeof(int)*(*data)[*size_level0][line].reducefrom_max);
          (*data)[*size_level0][line].reducefrom_max = 0;
          for (j = line + 1; j < size_level1b[*size_level0]; j++) {
            if ((*data)[*size_level0][j].frac == i) {
              (*data)[*size_level0][line].reducefrom[(*data)[*size_level0][line].reducefrom_max] = j;
              (*data)[*size_level0][line].reducefrom_max++;
            }
          }
          break;
        }
      }
    }
    (*size_level0)++;
  } else {
    size_level1b[*size_level0 - 1] = get_size_level1b(num_sockets, num_ports, step, 1);
  }
  size_level1b[*size_level0] = (*size_level1)[*size_level0] = size_level1b[*size_level0 - 1];
  (*data)[*size_level0] = (struct data_line*)malloc(sizeof(struct data_line)*size_level1b[*size_level0]);
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
  for (i = 0; i < num_sockets; i++){
    for (line = 0; line < size_level1b[*size_level0]; line++) {
      if ((*data)[*size_level0][line].frac == i) {
        (*data)[*size_level0][line].sendto_max = 1;
        (*data)[*size_level0][line].sendto = (int*)malloc(sizeof(int)*(*data)[*size_level0][line].sendto_max);
        (*data)[*size_level0][line].sendto[0] = -1;
        break;
      }
    }
  }
  (*size_level0)++;
  free(size_level1b);
  return 0;
error:
  free(size_level1b);
  return ERROR_MALLOC;
}

int ext_mpi_generate_allreduce_single(char *buffer_in, char *buffer_out) {
  struct data_line **data = NULL;
  struct parameters_block *parameters;
  int i;
  int size_level0 = 0, *size_level1 = NULL;
  int nbuffer_out = 0, nbuffer_in = 0;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  if (parameters->collective_type == collective_type_allgatherv) {
    i = allgather_core(&size_level0, &size_level1, &data, parameters->num_sockets, parameters->num_ports, parameters->socket);
  } else {
    i = allreduce_core(&size_level0, &size_level1, &data, parameters->num_sockets, parameters->num_ports, parameters->socket);
  }
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
