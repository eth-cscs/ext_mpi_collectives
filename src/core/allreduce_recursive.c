#include "allreduce_recursive.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int allgather_core(struct data_algorithm *data, int num_sockets, int *num_ports, int task) {
  int gbstep, i, step, line, gbstep_next;
  for (step = 0; num_ports[step]; step++);
  data->num_blocks = 0;
  data->blocks = (struct data_algorithm_block*)malloc(sizeof(struct data_algorithm_block)*(2*step+2));
  data->blocks[data->num_blocks].num_lines = 1;
  for (step = 0; num_ports[step]; step++) {
    data->blocks[data->num_blocks].num_lines *= (num_ports[step] + 1);
  }
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
    data->blocks[data->num_blocks].lines[line].frac = line;
    if (line == task) {
      data->blocks[data->num_blocks].lines[line].recvfrom_max = 1;
      data->blocks[data->num_blocks].lines[line].recvfrom_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
      data->blocks[data->num_blocks].lines[line].recvfrom_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
      data->blocks[data->num_blocks].lines[line].recvfrom_node[0] = -1;
      data->blocks[data->num_blocks].lines[line].recvfrom_line[0] = 0;
    }
  }
  data->num_blocks++;
  for (gbstep = 1, step = 0; num_ports[step]; gbstep *= num_ports[step] + 1, step++) {
    data->blocks[data->num_blocks].num_lines = data->blocks[data->num_blocks - 1].num_lines;
    data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
      data->blocks[data->num_blocks].lines[line].frac = line;
    }
    gbstep_next = gbstep * (num_ports[step] + 1);
    for (line = task / gbstep_next * gbstep_next; line < task / gbstep_next * gbstep_next + gbstep_next; line++) {
      if (line / gbstep == task / gbstep) {
        data->blocks[data->num_blocks].lines[line].sendto_max = num_ports[step];
        data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
        data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
        for (i = 0; i < data->blocks[data->num_blocks].lines[line].sendto_max; i++) {
          data->blocks[data->num_blocks].lines[line].sendto_node[i] = (task + gbstep * (i + 1)) % gbstep_next + task / gbstep_next * gbstep_next;
          data->blocks[data->num_blocks].lines[line].sendto_line[i] = line;
        }
      } else {
        data->blocks[data->num_blocks].lines[line].recvfrom_max = 1;
        data->blocks[data->num_blocks].lines[line].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
        data->blocks[data->num_blocks].lines[line].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
	if (line / gbstep > task / gbstep) {
	  i = (gbstep_next + line / gbstep - task / gbstep - 1) % gbstep_next;
	} else {
	  i = (gbstep_next + line / gbstep - task / gbstep) % gbstep_next - 1;
	}
        data->blocks[data->num_blocks].lines[line].recvfrom_node[0] = (task + gbstep * (i + 1)) % gbstep_next + task / gbstep_next * gbstep_next;
        data->blocks[data->num_blocks].lines[line].recvfrom_line[0] = line;
      }
    }
    data->num_blocks++;
  }
  data->blocks[data->num_blocks].num_lines = data->blocks[data->num_blocks - 1].num_lines;
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
    data->blocks[data->num_blocks].lines[line].frac = line;
    data->blocks[data->num_blocks].lines[line].sendto_max = 1;
    data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
    data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
    data->blocks[data->num_blocks].lines[line].sendto_node[0] = -1;
    data->blocks[data->num_blocks].lines[line].sendto_line[0] = data->blocks[data->num_blocks].lines[line].frac;
  }
  data->num_blocks++;
  return 0;
}

static int reduce_scatter_core(struct data_algorithm *data, int num_sockets, int *num_ports, int task) {
  int gbstep, i, step, line, gbstep_next, lines_base, last_round;
  for (step = 0; num_ports[step]; step++);
  data->num_blocks = 0;
  data->blocks = (struct data_algorithm_block*)malloc(sizeof(struct data_algorithm_block)*(2*step+2));
  lines_base = 1;
  for (step = 0; num_ports[step]; step++) {
    lines_base *= (abs(num_ports[step]) + 1);
  }
  data->blocks[data->num_blocks].num_lines = 2 * lines_base;
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  for (line = 0; line < lines_base; line++) {
    data->blocks[data->num_blocks].lines[line].frac = line;
    data->blocks[data->num_blocks].lines[line].recvfrom_max = 1;
    data->blocks[data->num_blocks].lines[line].recvfrom_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
    data->blocks[data->num_blocks].lines[line].recvfrom_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
    data->blocks[data->num_blocks].lines[line].recvfrom_node[0] = -1;
    data->blocks[data->num_blocks].lines[line].recvfrom_line[0] = line;
  }
  for (line = lines_base; line < lines_base * 2; line++) {
    data->blocks[data->num_blocks].lines[line].frac = line - lines_base;
  }
  data->num_blocks++;
  for (gbstep = 1, step = 0; num_ports[step]; gbstep *= abs(num_ports[step]) + 1, step++)
    ;
  for (step = 0; num_ports[step]; gbstep /= abs(num_ports[step]) + 1, step++) {
    last_round = !num_ports[step + 1];
    if (!last_round) {
      data->blocks[data->num_blocks].num_lines = (3 + abs(num_ports[step])) * lines_base;
    } else {
      data->blocks[data->num_blocks].num_lines = (2 + abs(num_ports[step])) * lines_base;
    }
    data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
      data->blocks[data->num_blocks].lines[line].frac = line % lines_base;
    }
    gbstep_next = gbstep / (abs(num_ports[step]) + 1);
    for (line = task / gbstep * gbstep; line < task / gbstep * gbstep + gbstep; line++) {
      if (line / gbstep_next != task / gbstep_next) {
        if (step > 0) {
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_max = 1;
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_max);
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_max);
	  if (line / gbstep_next > task / gbstep_next) {
	    i = (gbstep + line / gbstep_next - task / gbstep_next - 1) % gbstep;
	  } else {
	    i = (gbstep + line / gbstep_next - task / gbstep_next) % gbstep - 1;
	  }
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_line[0] = line;
        } else {
          data->blocks[data->num_blocks].lines[line].sendto_max = 1;
          data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
          data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
	  if (line / gbstep_next > task / gbstep_next) {
	    i = (gbstep + line / gbstep_next - task / gbstep_next - 1) % gbstep;
	  } else {
	    i = (gbstep + line / gbstep_next - task / gbstep_next) % gbstep - 1;
	  }
          data->blocks[data->num_blocks].lines[line].sendto_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
          data->blocks[data->num_blocks].lines[line].sendto_line[0] = line;
        }
      } else {
        if (!last_round) {
	  if (step > 0) {
            for (i = 0; i < abs(num_ports[step]); i++) {
	      data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max = 1;
              data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max);
              data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max);
              data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
              data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_line[0] = line;
            }
            data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max = abs(num_ports[step]);
            data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max);
            for (i = 0; i < data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max; i++) {
              data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom[i] = line + (3 + i) * lines_base;
            }
	  } else {
            for (i = 0; i < abs(num_ports[step]); i++) {
	      data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_max = 1;
              data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_max);
              data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_max);
              data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
              data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_line[0] = line;
            }
            data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max = abs(num_ports[step]);
            data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max);
	    data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom[0] = line;
            for (i = 1; i < data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max; i++) {
              data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom[i] = line + (2 + i) * lines_base;
            }
	  }
        } else {
	  data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_max = 1;
          data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_max);
          data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_max);
          i = abs(num_ports[step]) - 1;
          data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
          data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_line[0] = line;
          for (i = 0; i < abs(num_ports[step]) - 1; i++) {
	    data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max = 1;
            data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max);
            data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max);
            data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
            data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_line[0] = line;
          }
          data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max = abs(num_ports[step]);
          data->blocks[data->num_blocks].lines[line + lines_base].reducefrom = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max);
          if (step > 0) {
            for (i = 0; i < data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max; i++) {
              data->blocks[data->num_blocks].lines[line + lines_base].reducefrom[i] = line + (2 + i) * lines_base;
            }
          } else {
            data->blocks[data->num_blocks].lines[line + lines_base].reducefrom[0] = line;
            for (i = 1; i < data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max; i++) {
              data->blocks[data->num_blocks].lines[line + lines_base].reducefrom[i] = line + (2 + i) * lines_base;
            }
          }
        }
      }
    }
    data->num_blocks++;
  }
  data->blocks[data->num_blocks].num_lines = 2 * lines_base;
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  for (line = 0; line < lines_base; line++) {
    data->blocks[data->num_blocks].lines[line].frac = line;
  }
  for (line = lines_base; line < lines_base * 2; line++) {
    data->blocks[data->num_blocks].lines[line].frac = line - lines_base;
    if (line - data->blocks[data->num_blocks].num_lines / 2 == task) {
      data->blocks[data->num_blocks].lines[line].sendto_max = 1;
      data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
      data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
      data->blocks[data->num_blocks].lines[line].sendto_node[0] = -1;
      data->blocks[data->num_blocks].lines[line].sendto_line[0] = data->blocks[data->num_blocks].lines[line].frac;
    }
  }
  data->num_blocks++;
  return 0;
}

static int allreduce_core(struct data_algorithm *data, int num_sockets, int *num_ports, int task) {
  int gbstep, i, step, line, gbstep_next, lines_base, last_round, gbstep_middle;
  for (step = 0; num_ports[step]; step++);
  data->num_blocks = 0;
  data->blocks = (struct data_algorithm_block*)malloc(sizeof(struct data_algorithm_block)*(2*step+2));
  lines_base = 1;
  for (step = 0; num_ports[step]; step++) {
    if (num_ports[step] > 0) {
      lines_base *= (num_ports[step] + 1);
    }
  }
  data->blocks[data->num_blocks].num_lines = 2 * lines_base;
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  for (line = 0; line < lines_base; line++) {
    data->blocks[data->num_blocks].lines[line].frac = line;
    data->blocks[data->num_blocks].lines[line].recvfrom_max = 1;
    data->blocks[data->num_blocks].lines[line].recvfrom_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
    data->blocks[data->num_blocks].lines[line].recvfrom_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].recvfrom_max);
    data->blocks[data->num_blocks].lines[line].recvfrom_node[0] = -1;
    data->blocks[data->num_blocks].lines[line].recvfrom_line[0] = line;
  }
  for (line = lines_base; line < lines_base * 2; line++) {
    data->blocks[data->num_blocks].lines[line].frac = line - lines_base;
  }
  data->num_blocks++;
  gbstep = lines_base;
  for (step = 0; num_ports[step] < 0; gbstep /= abs(num_ports[step]) + 1, step++) {
    last_round = num_ports[step + 1] >= 0;
    if (!last_round) {
      data->blocks[data->num_blocks].num_lines = (3 + abs(num_ports[step])) * lines_base;
    } else {
      data->blocks[data->num_blocks].num_lines = (2 + abs(num_ports[step])) * lines_base;
    }
    data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
      data->blocks[data->num_blocks].lines[line].frac = line % lines_base;
    }
    gbstep_next = gbstep / (abs(num_ports[step]) + 1);
    for (line = task / gbstep * gbstep; line < task / gbstep * gbstep + gbstep; line++) {
      if (line / gbstep_next != task / gbstep_next) {
        if (step > 0) {
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_max = 1;
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_max);
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_max);
	  if (line / gbstep_next > task / gbstep_next) {
	    i = (gbstep + line / gbstep_next - task / gbstep_next - 1) % gbstep;
	  } else {
	    i = (gbstep + line / gbstep_next - task / gbstep_next) % gbstep - 1;
	  }
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
          data->blocks[data->num_blocks].lines[line + 2 * lines_base].sendto_line[0] = line;
        } else {
          data->blocks[data->num_blocks].lines[line].sendto_max = 1;
          data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
          data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
	  if (line / gbstep_next > task / gbstep_next) {
	    i = (gbstep + line / gbstep_next - task / gbstep_next - 1) % gbstep;
	  } else {
	    i = (gbstep + line / gbstep_next - task / gbstep_next) % gbstep - 1;
	  }
          data->blocks[data->num_blocks].lines[line].sendto_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
          data->blocks[data->num_blocks].lines[line].sendto_line[0] = line;
        }
      } else {
        if (!last_round) {
	  if (step > 0) {
            for (i = 0; i < abs(num_ports[step]); i++) {
	      data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max = 1;
              data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max);
              data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max);
              data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
              data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_line[0] = line;
            }
            data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max = abs(num_ports[step]);
            data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max);
            for (i = 0; i < data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max; i++) {
              data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom[i] = line + (3 + i) * lines_base;
            }
	  } else {
            for (i = 0; i < abs(num_ports[step]); i++) {
	      data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_max = 1;
              data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_max);
              data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_max);
              data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
              data->blocks[data->num_blocks].lines[line + (2 + i) * lines_base].recvfrom_line[0] = line;
            }
            data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max = abs(num_ports[step]);
            data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max);
	    data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom[0] = line;
            for (i = 1; i < data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom_max; i++) {
              data->blocks[data->num_blocks].lines[line + 2 * lines_base].reducefrom[i] = line + (2 + i) * lines_base;
            }
	  }
        } else {
	  data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_max = 1;
          data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_max);
          data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_max);
          i = abs(num_ports[step]) - 1;
          data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
          data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_line[0] = line;
          for (i = 0; i < abs(num_ports[step]) - 1; i++) {
	    data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max = 1;
            data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max);
            data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_max);
            data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_node[0] = (task + gbstep_next * (i + 1)) % gbstep + task / gbstep * gbstep;
            data->blocks[data->num_blocks].lines[line + (3 + i) * lines_base].recvfrom_line[0] = line;
          }
          data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max = abs(num_ports[step]);
          data->blocks[data->num_blocks].lines[line + lines_base].reducefrom = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max);
          if (step > 0) {
            for (i = 0; i < data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max; i++) {
              data->blocks[data->num_blocks].lines[line + lines_base].reducefrom[i] = line + (2 + i) * lines_base;
            }
          } else {
            data->blocks[data->num_blocks].lines[line + lines_base].reducefrom[0] = line;
            for (i = 1; i < data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max; i++) {
              data->blocks[data->num_blocks].lines[line + lines_base].reducefrom[i] = line + (2 + i) * lines_base;
            }
          }
        }
      }
    }
    data->num_blocks++;
  }
  gbstep_middle = gbstep;
  for (gbstep = 1; gbstep < gbstep_middle; gbstep *= num_ports[step] + 1, step++) {
    gbstep_next = gbstep * (num_ports[step] + 1);
    if (step == 0) {
      data->blocks[data->num_blocks].num_lines = (num_ports[step] + 1) * lines_base;
      data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
      memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
      for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
        data->blocks[data->num_blocks].lines[line].frac = line % lines_base;
      }
      for (line = task / gbstep_middle * gbstep_middle; line < task / gbstep_middle * gbstep_middle + gbstep_middle; line++) {
        data->blocks[data->num_blocks].lines[line].sendto_max = num_ports[step];
        data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
        data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
        data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max = num_ports[step];
        data->blocks[data->num_blocks].lines[line + lines_base].reducefrom = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max);
        for (i = 0; i < data->blocks[data->num_blocks].lines[line].sendto_max; i++) {
          data->blocks[data->num_blocks].lines[line].sendto_node[i] = (task + gbstep * (i + 1)) % gbstep_next + task / gbstep_next * gbstep_next;
          data->blocks[data->num_blocks].lines[line].sendto_line[i] = line;
	  if (i == 0) {
            data->blocks[data->num_blocks].lines[line + lines_base].reducefrom[i] = line;
	  } else {
            data->blocks[data->num_blocks].lines[line + lines_base].reducefrom[i] = line + (i + 1) * lines_base;
	  }
          data->blocks[data->num_blocks].lines[line + (i + 1) * lines_base].recvfrom_max = 1;
          data->blocks[data->num_blocks].lines[line + (i + 1) * lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (i + 1) * lines_base].recvfrom_max);
          data->blocks[data->num_blocks].lines[line + (i + 1) * lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (i + 1) * lines_base].recvfrom_max);
          data->blocks[data->num_blocks].lines[line + (i + 1) * lines_base].recvfrom_node[0] = (task + gbstep * (i + 1)) % gbstep_next + task / gbstep_next * gbstep_next;
          data->blocks[data->num_blocks].lines[line + (i + 1) * lines_base].recvfrom_line[0] = line;
        }
      }
    } else {
      data->blocks[data->num_blocks].num_lines = (num_ports[step] + 2) * lines_base;
      data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
      memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
      for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
        data->blocks[data->num_blocks].lines[line].frac = line % lines_base;
      }
      for (line = task / gbstep_middle * gbstep_middle; line < task / gbstep_middle * gbstep_middle + gbstep_middle; line++) {
        data->blocks[data->num_blocks].lines[line + lines_base].sendto_max = num_ports[step];
        data->blocks[data->num_blocks].lines[line + lines_base].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].sendto_max);
        data->blocks[data->num_blocks].lines[line + lines_base].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].sendto_max);
        data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max = num_ports[step];
        data->blocks[data->num_blocks].lines[line + lines_base].reducefrom = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].reducefrom_max);
        for (i = 0; i < data->blocks[data->num_blocks].lines[line + lines_base].sendto_max; i++) {
          data->blocks[data->num_blocks].lines[line + lines_base].sendto_node[i] = (task + gbstep * (i + 1)) % gbstep_next + task / gbstep_next * gbstep_next;
          data->blocks[data->num_blocks].lines[line + lines_base].sendto_line[i] = line;
          data->blocks[data->num_blocks].lines[line + lines_base].reducefrom[i] = line + (i + 2) * lines_base;
          data->blocks[data->num_blocks].lines[line + (i + 2) * lines_base].recvfrom_max = 1;
          data->blocks[data->num_blocks].lines[line + (i + 2) * lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (i + 2) * lines_base].recvfrom_max);
          data->blocks[data->num_blocks].lines[line + (i + 2) * lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + (i + 2) * lines_base].recvfrom_max);
          data->blocks[data->num_blocks].lines[line + (i + 2) * lines_base].recvfrom_node[0] = (task + gbstep * (i + 1)) % gbstep_next + task / gbstep_next * gbstep_next;
          data->blocks[data->num_blocks].lines[line + (i + 2) * lines_base].recvfrom_line[0] = line;
        }
      }
    }
    data->num_blocks++;
  }
  for (; num_ports[step]; gbstep *= num_ports[step] + 1, step++) {
    data->blocks[data->num_blocks].num_lines = 2 * lines_base;
    data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line)*data->blocks[data->num_blocks].num_lines);
    for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
      data->blocks[data->num_blocks].lines[line].frac = line % lines_base;
    }
    gbstep_next = gbstep * (num_ports[step] + 1);
    for (line = task / gbstep_next * gbstep_next; line < task / gbstep_next * gbstep_next + gbstep_next; line++) {
      if (line / gbstep == task / gbstep) {
        data->blocks[data->num_blocks].lines[line + lines_base].sendto_max = num_ports[step];
        data->blocks[data->num_blocks].lines[line + lines_base].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].sendto_max);
        data->blocks[data->num_blocks].lines[line + lines_base].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].sendto_max);
        for (i = 0; i < data->blocks[data->num_blocks].lines[line + lines_base].sendto_max; i++) {
          data->blocks[data->num_blocks].lines[line + lines_base].sendto_node[i] = (task + gbstep * (i + 1)) % gbstep_next + task / gbstep_next * gbstep_next;
          data->blocks[data->num_blocks].lines[line + lines_base].sendto_line[i] = line;
        }
      } else {
        data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_max = 1;
        data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_node = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_max);
        data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_line = (int *)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_max);
	if (line / gbstep > task / gbstep) {
	  i = (gbstep_next + line / gbstep - task / gbstep - 1) % gbstep_next;
	} else {
	  i = (gbstep_next + line / gbstep - task / gbstep) % gbstep_next - 1;
	}
        data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_node[0] = (task + gbstep * (i + 1)) % gbstep_next + task / gbstep_next * gbstep_next;
        data->blocks[data->num_blocks].lines[line + lines_base].recvfrom_line[0] = line;
      }
    }
    data->num_blocks++;
  }
  data->blocks[data->num_blocks].num_lines = 2 * lines_base;
  data->blocks[data->num_blocks].lines = (struct data_algorithm_line*)malloc(sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  memset(data->blocks[data->num_blocks].lines, 0, sizeof(struct data_algorithm_line) * data->blocks[data->num_blocks].num_lines);
  for (line = 0; line < data->blocks[data->num_blocks].num_lines; line++) {
    data->blocks[data->num_blocks].lines[line].frac = line % lines_base;
    if (line >= lines_base) {
      data->blocks[data->num_blocks].lines[line].sendto_max = 1;
      data->blocks[data->num_blocks].lines[line].sendto_node = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
      data->blocks[data->num_blocks].lines[line].sendto_line = (int*)malloc(sizeof(int)*data->blocks[data->num_blocks].lines[line].sendto_max);
      data->blocks[data->num_blocks].lines[line].sendto_node[0] = -1;
      data->blocks[data->num_blocks].lines[line].sendto_line[0] = data->blocks[data->num_blocks].lines[line].frac;
    }
  }
  data->num_blocks++;
  return 0;
}

int ext_mpi_generate_allreduce_recursive(char *buffer_in, char *buffer_out) {
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
    for (i = 0; parameters->num_ports[i]; i++)
      ;
    if (parameters->num_ports[i - 1] < 0) {
      i = reduce_scatter_core(&data, parameters->num_sockets, parameters->num_ports, parameters->socket);
    } else {
      i = allreduce_core(&data, parameters->num_sockets, parameters->num_ports, parameters->socket);
    }
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
