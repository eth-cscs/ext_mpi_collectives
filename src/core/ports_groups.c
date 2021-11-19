#include <stdlib.h>
#include <stdio.h>

char* ext_mpi_print_ports_groups(int *ports, int *groups){
  char *rvalue;
  int nrvalue=0, i, j, k;
  rvalue=malloc(10000);
  for (i=0; groups[i]&&ports[i]; i++){
    for (j=i; groups[j]>0; j++);
    nrvalue+=sprintf(rvalue+nrvalue, "%d(", abs(groups[i]));
    for (k=i; k<j; k++){
      nrvalue+=sprintf(rvalue+nrvalue, "%d ", -ports[k]);
    }
    nrvalue+=sprintf(rvalue+nrvalue, "%d) ", -ports[j]);
    i=j;
  }
  rvalue[nrvalue-1]=0;
  return rvalue;
}

int ext_mpi_scan_ports_groups(){
  return 0;
}

/*int main(){
  int ports[20], groups[20];
  char *c;
  groups[0] = 8; ports[0] = 1;
  groups[1] = 8; ports[1] = -1;
  groups[2] = 8; ports[2] = -1;
  groups[3] = -8; ports[3] = -1;
  groups[4] = -2; ports[4] = -1;
  c=ext_mpi_print_ports_groups(ports, groups);
  printf("%s\n", c);
  return 0;
}*/
