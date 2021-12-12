#include "alltoall.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_alltoall_get_text(char *buffer_in, char *buffer_out) {
  int task, num_nodes = -1, num_ports = -1;
  int i, gbstep, nstep, shift, nbuffer_out = 0, *source_array, from, to, flag,
                               integer1;
  char string1[100], string2[100], *buffer_in_new;
  do {
    if (sscanf(buffer_in, "%99s %99s %d", string1, string2, &integer1) > 0) {
      buffer_in_new = strchr(buffer_in, '\n');
      if (buffer_in_new) {
        *buffer_in_new = '\0';
      }
      flag = (strcmp(string1, "PARAMETER") == 0);
      if (flag) {
        nbuffer_out += sprintf(buffer_out + nbuffer_out, "%s\n", buffer_in);
        if (buffer_in_new) {
          buffer_in = buffer_in_new + 1;
        }
        if (strcmp(string2, "NODE") == 0) {
          task = integer1;
        }
        if (strcmp(string2, "NUM_NODES") == 0) {
          num_nodes = integer1;
        }
        if (strcmp(string2, "NUM_PORTS") == 0) {
          num_ports = integer1;
        }
      }
      if (buffer_in_new) {
        buffer_in = buffer_in_new + 1;
      }
    } else {
      flag = 0;
    }
  } while (flag);
  source_array = (int *)malloc(num_nodes * sizeof(int));
  for (i = 0; i < num_nodes; i++) {
    source_array[i] = 0;
  }
  for (gbstep = 1, nstep = 0; gbstep < num_nodes * (num_ports + 1);
       gbstep *= (num_ports + 1), nstep++) {
    for (i = 0; i < num_nodes; i++) {
      if (nstep > 0) {
        shift = ((i / (gbstep / (num_ports + 1))) % (num_ports + 1)) *
                (gbstep / (num_ports + 1));
        from = (num_nodes + task - shift) % num_nodes;
      } else {
        from = -1;
      }
      if (gbstep < num_nodes) {
        shift = ((i / gbstep) % (num_ports + 1)) * gbstep;
        to = (num_nodes + task + shift) % num_nodes;
      } else {
        to = -1;
      }
      nbuffer_out +=
          sprintf(buffer_out + nbuffer_out,
                  " STAGE %d FRAC %d SOURCE %d TO %d FROM %d|-1\n", nstep,
                  (i + task) % num_nodes, source_array[i], to, from);
      source_array[i] = (num_nodes + source_array[i] - to) % num_nodes;
    }
    nbuffer_out += sprintf(buffer_out + nbuffer_out, "#\n");
  }
  free(source_array);
  return (nbuffer_out);
}
