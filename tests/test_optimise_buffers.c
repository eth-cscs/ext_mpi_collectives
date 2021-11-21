#include "optimise_buffers.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  char *buffer_in, *buffer_out;
  int nbuffer_out = 0;
  buffer_in = (char *)malloc(MAX_BUFFER_SIZE);
  buffer_out = (char *)malloc(MAX_BUFFER_SIZE);
  buffer_in[fread(buffer_in, 1, MAX_BUFFER_SIZE, stdin)] = 0;
  if (argc > 1)
    switch_to_ascii(buffer_in);
  nbuffer_out = generate_optimise_buffers(buffer_in, buffer_out);
  fwrite(buffer_out, 1, nbuffer_out, stdout);
  free(buffer_out);
  free(buffer_in);
  return 0;
}