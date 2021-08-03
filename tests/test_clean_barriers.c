#include "clean_barriers.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  char *line, *buffer_in, *buffer_out, *line_return;
  int nbuffer_out = 0;
  line = malloc(MAX_BUFFER_SIZE);
  buffer_in = malloc(MAX_BUFFER_SIZE);
  buffer_out = malloc(MAX_BUFFER_SIZE);
  while (!feof(stdin)) {
    if ((line_return = fgets(line, MAX_BUFFER_SIZE, stdin))) {
      nbuffer_out += sprintf(buffer_in + nbuffer_out, "%s", line_return);
    }
  }
  clean_barriers(buffer_in, buffer_out);
  printf("%s", buffer_out);
  free(buffer_out);
  free(buffer_in);
  free(line);
}
