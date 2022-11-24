#include "raw_code_tasks_node.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  char *buffer_in, *buffer_out;
  int nbuffer_out = 0;
  buffer_in = (char *)malloc(MAX_BUFFER_SIZE);
  buffer_out = (char *)malloc(MAX_BUFFER_SIZE);
  buffer_in[fread(buffer_in, 1, MAX_BUFFER_SIZE, stdin)] = 0;
  if (argc > 1)
    ext_mpi_switch_to_ascii(buffer_in);
  nbuffer_out = ext_mpi_generate_raw_code_tasks_node(buffer_in, buffer_out);
  fwrite(buffer_out, 1, nbuffer_out, stdout);
  free(buffer_out);
  free(buffer_in);
  return 0;
}
