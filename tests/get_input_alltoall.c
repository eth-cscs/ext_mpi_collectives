#include "alltoall.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  int i, j;
  printf("%s\n", " PARAMETER SOCKET 0");
  printf("%s\n", " PARAMETER NUM_SOCKETS 8");
  printf("%s\n", " PARAMETER SOCKET_RANK 2");
  printf("%s\n", " PARAMETER SOCKET_ROW_SIZE 4");
  printf("%s\n", " PARAMETER SOCKET_COLUMN_SIZE 1");
  printf("%s\n", " PARAMETER NUM_PORTS 1");
  printf("%s", " PARAMETER MESSAGE_SIZE");
  for (i = 0; i < 8; i++) {
    for (j = 0; j < 8; j++) {
      printf(" %d", 256);
    }
  }
  printf("\n");
  printf("%s\n", " PARAMETER DATA_TYPE LONG_INT");
  return (0);
}
