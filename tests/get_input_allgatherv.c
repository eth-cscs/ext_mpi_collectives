#include "alltoall.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  int i, *counts, msize, *msizes, *iocounts;
  counts = (int *)malloc(sizeof(int) * 1);
  counts[0] = 80;
  iocounts = (int *)malloc(sizeof(int) * 4);
  iocounts[0] = 80;
  iocounts[1] = 160;
  iocounts[2] = 40;
  iocounts[3] = 48;
  msize = 0;
  for (i = 0; i < 1; i++) {
    msize += counts[i];
  }
  msizes = (int *)malloc(sizeof(int) * 8);
  for (i = 0; i < 8; i++) {
    msizes[i] = (msize / 8) / 8;
    if (i < (msize / 8) % 8) {
      msizes[i]++;
    }
    msizes[i] *= 8;
  }
  printf("%s\n", " PARAMETER COLLECTIVE_TYPE ALLGATHERV");
  printf("%s\n", " PARAMETER SOCKET 0");
  printf("%s\n", " PARAMETER NUM_SOCKETS 8");
  printf("%s\n", " PARAMETER SOCKET_RANK 0");
  printf("%s\n", " PARAMETER SOCKET_ROW_SIZE 4");
  printf("%s\n", " PARAMETER SOCKET_COLUMN_SIZE 1");
  printf("%s", " PARAMETER COUNTS");
  for (i = 0; i < 1; i++) {
    printf(" %d", counts[i]);
  }
  printf("\n");
  printf("%s", " PARAMETER IOCOUNTS");
  for (i = 0; i < 4; i++) {
    printf(" %d", iocounts[i]);
  }
  printf("\n");
  printf("%s\n", " PARAMETER NUM_PORTS 8(1 1 1)");
  printf("%s", " PARAMETER MESSAGE_SIZE");
  for (i = 0; i < 8; i++) {
    printf(" %d", msizes[i]);
  }
  printf("\n");
  printf("%s\n", " PARAMETER DATA_TYPE LONG_INT");
  printf("%s\n", " PARAMETER VERBOSE");
  printf("%s\n", " PARAMETER ASCII");
  free(iocounts);
  free(counts);
  return (0);
}
