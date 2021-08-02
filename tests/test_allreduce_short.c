#include <stdlib.h>
#include <stdio.h>
#include "allreduce_short.h"

#define MAX_BUF_SIZE 100000

int main(int argc, char *argv[]){
  char line[MAX_BUF_SIZE], buffer_in[MAX_BUF_SIZE], buffer_out[MAX_BUF_SIZE], *line_return;
  int nbuffer_out=0;
  while (!feof(stdin)){
    if ((line_return=fgets(line, MAX_BUF_SIZE, stdin))){
      nbuffer_out+=sprintf(buffer_in+nbuffer_out, "%s", line_return);
    }
  }
  generate_allreduce_short(buffer_in, buffer_out);
  printf("%s", buffer_out);
}
