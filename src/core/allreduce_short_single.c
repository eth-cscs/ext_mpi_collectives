#include "allreduce_short_single.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void get_lines_used(struct data_algorithm *data, int block_middle, int *lines_used) {
  int line, flag = 1, block;
  for (line = 0; line < data->blocks[data->num_blocks - 1].num_lines; line++) {
    lines_used[line] = 0;
  }
  for (line = 0; line < data->blocks[data->num_blocks - 1].num_lines; line++) {
    if (data->blocks[data->num_blocks - 1].lines[line].sendto_max == 1) {
      lines_used[line] = 1;
    }
  }
  while (flag) {
    flag = 0;
    for (block = data->num_blocks - 2; block >= block_middle; block--) {
      for (line = data->blocks[block].num_lines - 1; line >= 0; line--) {
	if (line < data->blocks[data->num_blocks - 1].num_lines && lines_used[line]) {
	  if (data->blocks[block].lines[line].reducefrom_max > 0 && !lines_used[data->blocks[block].lines[line].reducefrom[0]]) {
	    lines_used[data->blocks[block].lines[line].reducefrom[0]] = 1;
	    flag = 1;
	  }
	  if (data->blocks[block].lines[line].recvfrom_max > 0 && !lines_used[data->blocks[block].lines[line].recvfrom_line[0]]) {
	    lines_used[data->blocks[block].lines[line].recvfrom_line[0]] = 1;
	    flag = 1;
	  }
	}
      }
    }
  }
}

static void delete_lines(struct data_algorithm *data, int block_middle) {
  int block, line, lines_deleted, *lines_used, i, j;
  lines_used = (int *)malloc(sizeof(int) * data->blocks[data->num_blocks - 1].num_lines);
  get_lines_used(data, block_middle, lines_used);
  for (block = block_middle; block < data->num_blocks; block++) {
    if (block < data->num_blocks - 1){
      for (line = 0; line < data->blocks[block].num_lines; line++){
        if (lines_used[line] && data->blocks[block].lines[line].sendto_max > 0) {
	  for (i = 0; i < data->blocks[block].lines[line].sendto_max; i++) {
            if (!lines_used[data->blocks[block].lines[line].sendto_line[i]]) {
              data->blocks[block].lines[line].sendto_max--;
              for (j = i; j < data->blocks[block].lines[line].sendto_max; j++) {
	        data->blocks[block].lines[line].sendto_node[j] = data->blocks[block].lines[line].sendto_node[j + 1];
	        data->blocks[block].lines[line].sendto_line[j] = data->blocks[block].lines[line].sendto_line[j + 1];
	      }
	      i--;
	    }
	  }
	  if (data->blocks[block].lines[line].sendto_max == 0) {
	    free(data->blocks[block].lines[line].sendto_node);
	    free(data->blocks[block].lines[line].sendto_line);
	    data->blocks[block].lines[line].sendto_node = data->blocks[block].lines[line].sendto_line = NULL;
	  }
        }
      }
    }
    lines_deleted = 0;
    for (line = 0; line < data->blocks[block].num_lines; line++){
      if (!lines_used[line + lines_deleted]) {
	ext_mpi_delete_stage_line(data->blocks[block].lines[line]);
	data->blocks[block].num_lines--;
	for (i = line; i < data->blocks[block].num_lines; i++) {
	  data->blocks[block].lines[i] = data->blocks[block].lines[i + 1];
	}
	for (i = 0; i < data->blocks[block].num_lines; i++) {
	  for (j = 0; j < data->blocks[block].lines[i].sendto_max; j++) {
	    if (data->blocks[block].lines[i].sendto_line[j] > line && data->blocks[block].lines[i].sendto_node[j] >= 0) {
	      data->blocks[block].lines[i].sendto_line[j]--;
	    }
	  }
	  for (j = 0; j < data->blocks[block].lines[i].recvfrom_max; j++) {
	    if (data->blocks[block].lines[i].recvfrom_line[j] > line) {
	      data->blocks[block].lines[i].recvfrom_line[j]--;
	    }
	  }
	  for (j = 0; j < data->blocks[block].lines[i].reducefrom_max; j++) {
	    if (data->blocks[block].lines[i].reducefrom[j] > line) {
	      data->blocks[block].lines[i].reducefrom[j]--;
	    }
	  }
        }
	line--;
	lines_deleted++;
      }
    }
  }
  free(lines_used);
}

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

static int allreduce_core(struct data_algorithm *data, int num_sockets, int *num_ports, int task) {
  int gbstep, i, j, k, l, step, line, *size_level1b, block_middle;
  for (step = 0; num_ports[step]; step++);
  data->num_blocks = 0;
  size_level1b = (int*)malloc(sizeof(int)*(2*step+2));
  if (!size_level1b) goto error;
  data->blocks = (struct data_algorithm_block*)malloc(sizeof(struct data_algorithm_block)*(2*step+2));
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*num_sockets*(abs(num_ports[0]) + 1));
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*num_sockets*(abs(num_ports[0]) + 1));
  size_level1b[data->num_blocks] = data->blocks[data->num_blocks].num_lines = 1;
  for (line = 0; line < size_level1b[data->num_blocks]; line++) {
    data->blocks[data->num_blocks].lines[line].frac = 0;
    data->blocks[data->num_blocks].lines[line].recvfrom_max = 1;
    data->blocks[data->num_blocks].lines[line].recvfrom_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
    data->blocks[data->num_blocks].lines[line].recvfrom_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
    data->blocks[data->num_blocks].lines[line].recvfrom_node[0] = -1;
    data->blocks[data->num_blocks].lines[line].recvfrom_line[0] = line;
  }
  data->num_blocks++;
  step = 0;
  block_middle = data->num_blocks;
  if (num_ports[step]) {
    for (gbstep = 1; num_ports[step]; gbstep *= num_ports[step] + 1, step++) {
      size_level1b[data->num_blocks] = data->blocks[data->num_blocks].num_lines = get_size_level1b(num_sockets, num_ports, step, 1);
      data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
      memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
      l = size_level1b[data->num_blocks] - get_size_level1b(num_sockets, num_ports, step, 0);
      for (line = 0; line < size_level1b[data->num_blocks]; line++) {
        if (line < get_size_level1b(num_sockets, num_ports, step, 0) && line >= size_level1b[data->num_blocks] - get_size_level1b(num_sockets, num_ports, step, 0) * num_ports[step] && line < l) {
          data->blocks[data->num_blocks].lines[line].frac = 3;
	} else if (line < get_size_level1b(num_sockets, num_ports, step, 0) && line >= l) {
          data->blocks[data->num_blocks].lines[line].frac = 1;
        } else if (line < get_size_level1b(num_sockets, num_ports, step, 0)) {
          data->blocks[data->num_blocks].lines[line].frac = 2;
        } else {
	  k = line - get_size_level1b(num_sockets, num_ports, step, 0);
	  k = k / get_size_level1b(num_sockets, num_ports, step, 0);
          data->blocks[data->num_blocks].lines[line].frac = -k - 1;
        }
      }
      for (line = size_level1b[data->num_blocks] - 1; line >= 0; line--) {
        switch (data->blocks[data->num_blocks].lines[line].frac) {
	  case 1:
            data->blocks[data->num_blocks].lines[line].frac = 0;
	  break;
	  case 2:
            data->blocks[data->num_blocks].lines[line].sendto_max = num_ports[step];
            data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
            data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
            for (i = 0; i < data->blocks[data->num_blocks].lines[line].sendto_max; i++) {
              data->blocks[data->num_blocks].lines[line].sendto_node[i] = (num_sockets + task - (i + 1) * gbstep) % num_sockets;
              data->blocks[data->num_blocks].lines[line].sendto_line[i] = line + get_size_level1b(num_sockets, num_ports, step, 0) * (i + 1);
            }
            data->blocks[data->num_blocks].lines[line].frac = 0;
	  break;
	  case 3:
            data->blocks[data->num_blocks].lines[line].sendto_max = num_ports[step] - 1;
            data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
            data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
            for (i = 0; i < data->blocks[data->num_blocks].lines[line].sendto_max; i++) {
              data->blocks[data->num_blocks].lines[line].sendto_node[i] = (num_sockets + task - (i + 1) * gbstep) % num_sockets;
              data->blocks[data->num_blocks].lines[line].sendto_line[i] = line + get_size_level1b(num_sockets, num_ports, step, 0) * (i + 1);
            }
            data->blocks[data->num_blocks].lines[line].frac = 0;
	  break;
          default:
            for (j = 0; data->blocks[data->num_blocks].lines[line - j].frac == data->blocks[data->num_blocks].lines[line].frac; j++)
              ;
            j--;
            data->blocks[data->num_blocks].lines[line].frac = -data->blocks[data->num_blocks].lines[line].frac;
            data->blocks[data->num_blocks].lines[line].recvfrom_max = 1;
            data->blocks[data->num_blocks].lines[line].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
            data->blocks[data->num_blocks].lines[line].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
            data->blocks[data->num_blocks].lines[line].recvfrom_node[0] = (num_sockets + task + data->blocks[data->num_blocks].lines[line].frac * gbstep) % num_sockets;
            data->blocks[data->num_blocks].lines[line].recvfrom_line[0] = j;
            data->blocks[data->num_blocks].lines[line].frac = 0;
        }
      }
      data->num_blocks++;
      size_level1b[data->num_blocks] = data->blocks[data->num_blocks].num_lines = data->blocks[data->num_blocks - 1].num_lines;
      data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
      memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
      for (line = 0; line < size_level1b[data->num_blocks]; line++) {
        data->blocks[data->num_blocks].lines[line].frac = 0;
      }
      for (i = 0; i < num_sockets; i++){
        for (line = 0; line < size_level1b[data->num_blocks]; line++) {
          if (data->blocks[data->num_blocks - 1].lines[line].recvfrom_max == 1 && data->blocks[data->num_blocks].lines[line].frac == i) {
            for (j = line - 1; j>= 0; j--) {
              if (data->blocks[data->num_blocks].lines[j].frac == i && (data->blocks[data->num_blocks - 1].lines[j].recvfrom_max != 1 || data->blocks[data->num_blocks - 1].lines[line].recvfrom_node[0] != data->blocks[data->num_blocks - 1].lines[j].recvfrom_node[0])) {
	        break;
              }
            }
	    if (j >= 0) {
              data->blocks[data->num_blocks].lines[line].reducefrom_max = 1;
              data->blocks[data->num_blocks].lines[line].reducefrom = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].reducefrom_max);
              data->blocks[data->num_blocks].lines[line].reducefrom[data->blocks[data->num_blocks].lines[line].reducefrom_max - 1] = j;
	    }
          }
        }
      }
      data->num_blocks++;
    }
  } else {
    size_level1b[data->num_blocks - 1] = get_size_level1b(num_sockets, num_ports, step, 1);
  }
  size_level1b[data->num_blocks] = data->blocks[data->num_blocks].num_lines = size_level1b[data->num_blocks - 1];
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*size_level1b[data->num_blocks]);
  for (line = 0; line < size_level1b[data->num_blocks]; line++) {
    data->blocks[data->num_blocks].lines[line].frac = 0;
  }
  for (i = 0; i < num_sockets; i++){
    for (line = size_level1b[data->num_blocks] - 1; line >= 0; line--) {
      if (data->blocks[data->num_blocks].lines[line].frac == i) {
        data->blocks[data->num_blocks].lines[line].sendto_max = 1;
        data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
        data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
        data->blocks[data->num_blocks].lines[line].sendto_node[0] = -1;
        data->blocks[data->num_blocks].lines[line].sendto_line[0] = data->blocks[data->num_blocks].lines[line].frac;
        break;
      }
    }
  }
  data->num_blocks++;
  free(size_level1b);
  if (block_middle < data->num_blocks - 1) {
    delete_lines(data, block_middle);
  }
  return 0;
error:
  free(size_level1b);
  return ERROR_MALLOC;
}

int ext_mpi_generate_allreduce_short_single(char *buffer_in, char *buffer_out) {
  struct data_algorithm data;
  struct parameters_block *parameters;
  int i;
  int nbuffer_out = 0, nbuffer_in = 0;
  data.num_blocks = 0;
  data.blocks = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  for (i = 1; i < parameters->message_sizes_max; i++) {
    if (parameters->message_sizes[0] != parameters->message_sizes[i]) {
      printf("only equal message sizes are supported for short single algorithm\n");
      exit(1);
    }
  }
  parameters->message_sizes[0] *= parameters->message_sizes_max;
  parameters->message_sizes_max = 1;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  if (parameters->collective_type == collective_type_allgatherv) {
    printf("ext_mpi_generate_allreduce_short not implemented\n");
    exit(1);
  } else {
    i = allreduce_core(&data, parameters->num_nodes, parameters->num_ports, parameters->node);
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
