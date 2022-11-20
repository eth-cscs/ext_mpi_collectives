#include "allreduce_single.h"
#include "constants.h"
#include "read_write.h"
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

static int allgather_core(struct data_algorithm *data, int num_sockets, int *num_ports, int task) {
  int gbstep, i, l, step, line;
  for (step = 0; num_ports[step]; step++);
  data->num_blocks = 0;
  data->blocks = (struct data_algorithm_block*)malloc(sizeof(struct data_algorithm_block)*(2*step+2));
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line) * 1);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line) * 1);
  data->blocks[data->num_blocks].num_lines = 1;
  data->blocks[data->num_blocks].lines[0].frac = task;
  data->blocks[data->num_blocks].lines[0].recvfrom_max = 1;
  data->blocks[data->num_blocks].lines[0].recvfrom_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[0].recvfrom_max);
  data->blocks[data->num_blocks].lines[0].recvfrom_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[0].recvfrom_max);
  data->blocks[data->num_blocks].lines[0].recvfrom_node[0] = -1;
  data->blocks[data->num_blocks].lines[0].recvfrom_line[0] = 0;
  data->num_blocks++;
  for (step = 0, gbstep = 1; num_ports[step]; gbstep *= num_ports[step] + 1, step++) {
    data->blocks[data->num_blocks].num_lines = data->blocks[data->num_blocks - 1].num_lines * (num_ports[step] + 1);
    if (data->blocks[data->num_blocks].num_lines > num_sockets) data->blocks[data->num_blocks].num_lines = num_sockets;
    data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
      l = gbstep;
      if (l > data->blocks[data->num_blocks].num_lines - gbstep) l = data->blocks[data->num_blocks].num_lines - gbstep;
      if (line < l) {
        data->blocks[data->num_blocks].lines[line].sendto_max = num_ports[step];
        if (line >= data->blocks[data->num_blocks].num_lines - num_ports[step] * data->blocks[data->num_blocks - 1].num_lines) data->blocks[data->num_blocks].lines[line].sendto_max--;
        data->blocks[data->num_blocks].lines[line].sendto = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
        for (i = 0; i < data->blocks[data->num_blocks].lines[line].sendto_max; i++) {
          data->blocks[data->num_blocks].lines[line].sendto[i] = (task + num_sockets - (i + 1) * gbstep) % num_sockets;
        }
      } else if (line >= gbstep) {
        data->blocks[data->num_blocks].lines[line].recvfrom_max = 1;
        data->blocks[data->num_blocks].lines[line].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
        data->blocks[data->num_blocks].lines[line].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
        data->blocks[data->num_blocks].lines[line].recvfrom_node[0] = (task + num_sockets + (line / gbstep) * gbstep) % num_sockets;
        data->blocks[data->num_blocks].lines[line].recvfrom_line[0] = line % gbstep;
      }
      data->blocks[data->num_blocks].lines[line].frac = (task + line) % num_sockets;
    }
    data->num_blocks++;
  }
  data->blocks[data->num_blocks].num_lines = data->blocks[data->num_blocks - 1].num_lines;
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
  for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
    data->blocks[data->num_blocks].lines[line].frac = data->blocks[data->num_blocks - 1].lines[line].frac;
    data->blocks[data->num_blocks].lines[line].sendto_max = 1;
    data->blocks[data->num_blocks].lines[line].sendto = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
    data->blocks[data->num_blocks].lines[line].sendto[0] = -1;
  }
  data->num_blocks++;
  return 0;
}

static int allreduce_core(struct data_algorithm *data, int num_sockets, int *num_ports, int task) {
  int gbstep, i, j, k, l, m, step, line, line2, *size_level1b;
  for (step = 0; num_ports[step]; step++);
  data->num_blocks = 0;
  size_level1b = (int*)malloc(sizeof(int)*(2*step+2));
  if (!size_level1b) goto error;
  data->blocks = (struct data_algorithm_block*)malloc(sizeof(struct data_algorithm_block)*(2*step+2));
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*num_sockets*(abs(num_ports[0]) + 1));
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*num_sockets*(abs(num_ports[0]) + 1));
  size_level1b[data->num_blocks] = data->blocks[data->num_blocks].num_lines = num_sockets;
  for (line = 0; line < size_level1b[data->num_blocks]; line++) {
    data->blocks[data->num_blocks].lines[line].frac = (line + task) % num_sockets;
    data->blocks[data->num_blocks].lines[line].recvfrom_max = 1;
    data->blocks[data->num_blocks].lines[line].recvfrom_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
    data->blocks[data->num_blocks].lines[line].recvfrom_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
    data->blocks[data->num_blocks].lines[line].recvfrom_node[0] = -1;
    data->blocks[data->num_blocks].lines[line].recvfrom_line[0] = line;
  }
  data->num_blocks++;
  step = 0;
  if (num_ports[step] < 0) {
    for (gbstep = (num_sockets - 1) / (-num_ports[step] + 1) + 1; num_ports[step] < 0; step++, gbstep = (gbstep - 1) / (-num_ports[step] + 1) + 1) {
      size_level1b[data->num_blocks] = data->blocks[data->num_blocks].num_lines = get_size_level1b(num_sockets, num_ports, step, 1);
      data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks - 1]*(-num_ports[step] + 1));
      memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks - 1]*(-num_ports[step] + 1));
      for (line = 0; line < size_level1b[data->num_blocks]; line++) {
        data->blocks[data->num_blocks].lines[line].frac = data->blocks[data->num_blocks - 1].lines[line].frac;
      }
      for (i = line = 0; i < abs(num_ports[step]) + 1; i++) {
        j = size_level1b[data->num_blocks] / (abs(num_ports[step]) + 1);
        if (i < size_level1b[data->num_blocks] % (abs(num_ports[step]) + 1)) j++;
        for (l = 0; l < j; l++) {
          if (i > 0) {
            data->blocks[data->num_blocks].lines[line].sendto_max = 1;
            data->blocks[data->num_blocks].lines[line].sendto = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
            data->blocks[data->num_blocks].lines[line].sendto[0] = (task + gbstep * i) % num_sockets;
            data->blocks[data->num_blocks].lines[data->blocks[data->num_blocks].num_lines].frac = data->blocks[data->num_blocks].lines[l].frac;
            data->blocks[data->num_blocks].lines[data->blocks[data->num_blocks].num_lines].recvfrom_max = 1;
            data->blocks[data->num_blocks].lines[data->blocks[data->num_blocks].num_lines].recvfrom_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[data->blocks[data->num_blocks].num_lines].recvfrom_max);
            data->blocks[data->num_blocks].lines[data->blocks[data->num_blocks].num_lines].recvfrom_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[data->blocks[data->num_blocks].num_lines].recvfrom_max);
            data->blocks[data->num_blocks].lines[data->blocks[data->num_blocks].num_lines].recvfrom_node[0] = (num_sockets + task - gbstep * i) % num_sockets;
            data->blocks[data->num_blocks].lines[data->blocks[data->num_blocks].num_lines].recvfrom_line[0] = line;
            data->blocks[data->num_blocks].num_lines++;
          }
          line++;
        }
      }
      data->num_blocks++;
      data->blocks[data->num_blocks].num_lines = data->blocks[data->num_blocks - 1].num_lines;
      data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
      memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
      size_level1b[data->num_blocks] = size_level1b[data->num_blocks - 1];
      for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
        data->blocks[data->num_blocks].lines[line].frac = data->blocks[data->num_blocks - 1].lines[line].frac;
        data->blocks[data->num_blocks].lines[line].reducefrom = (int*)malloc(sizeof(int)*abs(num_ports[step]));
      }
      for (line = 0; line < size_level1b[data->num_blocks]; line++) {
        for (line2 = size_level1b[data->num_blocks]; line2 < data->blocks[data->num_blocks].num_lines; line2++) {
          if (data->blocks[data->num_blocks].lines[line].frac == data->blocks[data->num_blocks].lines[line2].frac) {
            data->blocks[data->num_blocks].lines[line].reducefrom[data->blocks[data->num_blocks].lines[line].reducefrom_max++] = line2;
          }
        }
      }
      data->num_blocks++;
    }
  }
  if (num_ports[step]) {
    for (gbstep = 1; num_ports[step]; gbstep *= num_ports[step] + 1, step++) {
      size_level1b[data->num_blocks] = data->blocks[data->num_blocks].num_lines = get_size_level1b(num_sockets, num_ports, step, 1);
      data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
      memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
      l = size_level1b[data->num_blocks] - get_size_level1b(num_sockets, num_ports, step, 0);
      i = l / num_ports[step];
      j = l % num_ports[step];
      for (line = 0; line < size_level1b[data->num_blocks]; line++) {
        if (line < get_size_level1b(num_sockets, num_ports, step, 0) && line >= l) {
          data->blocks[data->num_blocks].lines[line].frac = -1;
        } else if (line < get_size_level1b(num_sockets, num_ports, step, 0)) {
          data->blocks[data->num_blocks].lines[line].frac = task;
        } else if (line >= (size_level1b[data->num_blocks] - 1) / (num_ports[step] + 1) + 1) {
          for (k = 0; k < num_ports[step]; k++) {
            for (m = 0; m < (k < j ? i + 1 : i) && line < size_level1b[data->num_blocks]; m++) {
              data->blocks[data->num_blocks].lines[line].frac = -10 - (k + 1);
              line++;
            }
          }
        } else {
          printf("logical error in allreduce_core\n");
          exit(1);
        }
      }
      for (line = size_level1b[data->num_blocks] - 1; line >= 0; line--) {
        if (data->blocks[data->num_blocks].lines[line].frac == task) {
          data->blocks[data->num_blocks].lines[line].sendto_max = num_ports[step];
          data->blocks[data->num_blocks].lines[line].sendto = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
          for (i = 0; i < data->blocks[data->num_blocks].lines[line].sendto_max; i++) {
            data->blocks[data->num_blocks].lines[line].sendto[i] = (num_sockets + task - (i + 1) * gbstep) % num_sockets;
          }
          data->blocks[data->num_blocks].lines[line].frac = data->blocks[data->num_blocks - 1].lines[line].frac;
        } else if (data->blocks[data->num_blocks].lines[line].frac <= -10) {
          for (j = 0; data->blocks[data->num_blocks].lines[line - j].frac == data->blocks[data->num_blocks].lines[line].frac; j++)
            ;
          j--;
          data->blocks[data->num_blocks].lines[line].frac = -data->blocks[data->num_blocks].lines[line].frac - 10;
          data->blocks[data->num_blocks].lines[line].recvfrom_max = 1;
          data->blocks[data->num_blocks].lines[line].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
          data->blocks[data->num_blocks].lines[line].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
          data->blocks[data->num_blocks].lines[line].recvfrom_node[0] = (num_sockets + task + data->blocks[data->num_blocks].lines[line].frac * gbstep) % num_sockets;
          data->blocks[data->num_blocks].lines[line].recvfrom_line[0] = j;
          data->blocks[data->num_blocks].lines[line].frac = (data->blocks[data->num_blocks - 1].lines[j].frac + num_sockets + data->blocks[data->num_blocks].lines[line].frac * gbstep) % num_sockets;
        } else {
          data->blocks[data->num_blocks].lines[line].frac = data->blocks[data->num_blocks - 1].lines[line].frac;
        }
      }
      data->num_blocks++;
    }
    size_level1b[data->num_blocks] = data->blocks[data->num_blocks].num_lines = data->blocks[data->num_blocks - 1].num_lines;
    data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
    memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
    for (line = 0; line < size_level1b[data->num_blocks]; line++) {
      data->blocks[data->num_blocks].lines[line].frac = data->blocks[data->num_blocks - 1].lines[line].frac;
    }
    for (i = 0; i < num_sockets; i++){
      for (line = 0; line < size_level1b[data->num_blocks]; line++) {
        if (data->blocks[data->num_blocks].lines[line].frac == i) {
          for (j = line + 1; j < size_level1b[data->num_blocks]; j++) {
            if (data->blocks[data->num_blocks].lines[j].frac == i) {
              data->blocks[data->num_blocks].lines[line].reducefrom_max++;
            }
          }
          data->blocks[data->num_blocks].lines[line].reducefrom = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].reducefrom_max);
          data->blocks[data->num_blocks].lines[line].reducefrom_max = 0;
          for (j = line + 1; j < size_level1b[data->num_blocks]; j++) {
            if (data->blocks[data->num_blocks].lines[j].frac == i) {
              data->blocks[data->num_blocks].lines[line].reducefrom[data->blocks[data->num_blocks].lines[line].reducefrom_max] = j;
              data->blocks[data->num_blocks].lines[line].reducefrom_max++;
            }
          }
          break;
        }
      }
    }
    data->num_blocks++;
  } else {
    size_level1b[data->num_blocks - 1] = get_size_level1b(num_sockets, num_ports, step, 1);
  }
  size_level1b[data->num_blocks] = data->blocks[data->num_blocks].num_lines = size_level1b[data->num_blocks - 1];
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
  for (line = 0; line < size_level1b[data->num_blocks]; line++) {
    data->blocks[data->num_blocks].lines[line].frac = data->blocks[data->num_blocks - 1].lines[line].frac;
  }
  for (i = 0; i < num_sockets; i++){
    for (line = 0; line < size_level1b[data->num_blocks]; line++) {
      if (data->blocks[data->num_blocks].lines[line].frac == i) {
        data->blocks[data->num_blocks].lines[line].sendto_max = 1;
        data->blocks[data->num_blocks].lines[line].sendto = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
        data->blocks[data->num_blocks].lines[line].sendto[0] = -1;
        break;
      }
    }
  }
  data->num_blocks++;
  free(size_level1b);
  return 0;
error:
  free(size_level1b);
  return ERROR_MALLOC;
}

int ext_mpi_generate_allreduce_single(char *buffer_in, char *buffer_out) {
  struct data_algorithm data;
  struct parameters_block *parameters;
  int i;
  int nbuffer_out = 0, nbuffer_in = 0;
  data.num_blocks = 0;
  data.blocks = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  if (parameters->collective_type == collective_type_allgatherv) {
    i = allgather_core(&data, parameters->num_sockets, parameters->num_ports, parameters->socket);
  } else {
    i = allreduce_core(&data, parameters->num_sockets, parameters->num_ports, parameters->socket);
  }
  if (i < 0)
    goto error;
  nbuffer_out +=
      ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(data);
  nbuffer_out += ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
