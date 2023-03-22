#include "allreduce_recursive_shrink.h"
#include "constants.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

int ext_mpi_generate_allreduce_recursive_shrink(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, shrink = 1, block, line, i, j;
  struct data_algorithm data;
  struct parameters_block *parameters;
  data.num_blocks = 0;
  data.blocks = NULL;
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  nbuffer_in += i = ext_mpi_read_algorithm(buffer_in + nbuffer_in, &data, parameters->ascii_in);
  if (i == ERROR_MALLOC)
    goto error;
  if (i <= 0) {
    printf("error reading algorithm reduce_copyin\n");
    exit(2);
  }
  shrink = abs(parameters->groups[0]);
  for (i = 0; parameters->num_ports[i] < 0; i++) {
    shrink /= abs(parameters->num_ports[i]) + 1;
  }
  if (shrink > 1) {
    parameters->message_sizes_max /= shrink;
    for (i = 0; i < parameters->message_sizes_max; i++) {
      parameters->message_sizes[i] = parameters->message_sizes[i * shrink];
      for (j = 1; j < shrink; j++) {
        parameters->message_sizes[i] += parameters->message_sizes[i * shrink + j];
      }
    }
    for (block = 0; block < data.num_blocks; block++){
      data.blocks[block].num_lines /= shrink;
      for (line = 0; line < data.blocks[block].num_lines; line++) {
        data.blocks[block].lines[line].frac = data.blocks[block].lines[line * shrink].frac / shrink;
        data.blocks[block].lines[line].recvfrom_max = data.blocks[block].lines[line * shrink].recvfrom_max;
        data.blocks[block].lines[line].recvfrom_node = data.blocks[block].lines[line * shrink].recvfrom_node;
        data.blocks[block].lines[line].recvfrom_line = data.blocks[block].lines[line * shrink].recvfrom_line;
        for (i = 0; i < data.blocks[block].lines[line].recvfrom_max; i++) {
          data.blocks[block].lines[line].recvfrom_line[i] /= shrink;
        }
        data.blocks[block].lines[line].sendto_max = data.blocks[block].lines[line * shrink].sendto_max;
        data.blocks[block].lines[line].sendto_node = data.blocks[block].lines[line * shrink].sendto_node;
        data.blocks[block].lines[line].sendto_line = data.blocks[block].lines[line * shrink].sendto_line;
        for (i = 0; i < data.blocks[block].lines[line].sendto_max; i++) {
          data.blocks[block].lines[line].sendto_line[i] /= shrink;
        }
        data.blocks[block].lines[line].reducefrom_max = data.blocks[block].lines[line * shrink].reducefrom_max;
        data.blocks[block].lines[line].reducefrom = data.blocks[block].lines[line * shrink].reducefrom;
        for (i = 0; i < data.blocks[block].lines[line].reducefrom_max; i++) {
          data.blocks[block].lines[line].reducefrom[i] /= shrink;
        }
        data.blocks[block].lines[line].copyreducefrom_max = data.blocks[block].lines[line * shrink].copyreducefrom_max;
        data.blocks[block].lines[line].copyreducefrom = data.blocks[block].lines[line * shrink].copyreducefrom;
        for (i = 0; i < data.blocks[block].lines[line].copyreducefrom_max; i++) {
          data.blocks[block].lines[line].copyreducefrom[i] /= shrink;
        }
        free(data.blocks[block].lines[line * shrink + 1].recvfrom_node);
        free(data.blocks[block].lines[line * shrink + 1].recvfrom_line);
        free(data.blocks[block].lines[line * shrink + 1].sendto_node);
        free(data.blocks[block].lines[line * shrink + 1].sendto_line);
        free(data.blocks[block].lines[line * shrink + 1].reducefrom);
        free(data.blocks[block].lines[line * shrink + 1].copyreducefrom);
      }
    }
  }
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_out += ext_mpi_write_algorithm(data, buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  ext_mpi_delete_algorithm(data);
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
