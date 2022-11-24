#include <stdio.h>
#include <stdlib.h>
#include "read_write.h"
#include "clean_barriers.h"

int main(int argc, char *argv[]) {
  char *line, *buffer_in, *buffer_out, *line_return;
  int nbuffer_out = 0;
  line = (char *)malloc(MAX_BUFFER_SIZE);
  buffer_in = (char *)malloc(MAX_BUFFER_SIZE);
  buffer_out = (char *)malloc(MAX_BUFFER_SIZE);
  while (!feof(stdin)) {
    if ((line_return = fgets(line, MAX_BUFFER_SIZE, stdin))) {
      nbuffer_out += sprintf(buffer_in + nbuffer_out, "%s", line_return);
    }
  }
//FIXME  clean_barriers(buffer_in, buffer_out);
  printf("%s", buffer_out);
  free(buffer_out);
  free(buffer_in);
  free(line);
}
