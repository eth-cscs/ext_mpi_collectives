#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ports_groups.h"

char* ext_mpi_print_copyin(int copyin_method, int *copyin_factors) {
  char *rvalue;
  int nrvalue=0, i;
  rvalue=(char *)malloc(10000);
  nrvalue+=sprintf(rvalue+nrvalue, "%d;", copyin_method);
  for (i=0; copyin_factors[i]; i++){
    nrvalue+=sprintf(rvalue+nrvalue, "%d ", copyin_factors[i]);
  }
  rvalue[nrvalue-1]=0;
  return rvalue;
}

int ext_mpi_scan_copyin(char *str, int *copyin_method, int **copyin_factors) {
  char *c, *c2;
  int i = 0;
  c=c2=strdup(str);
  while (c[i] != ';' && c[i] != 0) i++;
  if (c[i] == ';') c[i] = ' ';
  sscanf(c2, "%d", copyin_method);
  *copyin_factors = (int*)malloc(1000*sizeof(int));
  i = 0;
  while (c2[0] != 0) {
    while (c2[0] != ' ' && c2[0] != 0) c2++;
    while (c2[0] == ' ' && c2[0] != 0) c2++;
    if (c2[0] != 0) sscanf(c2, "%d", &((*copyin_factors)[i++]));
  }
  (*copyin_factors)[i] = 0;
  free(c);
  return 0;
}

char* ext_mpi_print_ports_groups(int *ports, int *groups){
  char *rvalue;
  int nrvalue=0, i, j, k;
  rvalue=(char *)malloc(10000);
  for (i=0; groups[i]&&ports[i]; i++){
    for (j=i; groups[j]>0; j++);
    nrvalue+=sprintf(rvalue+nrvalue, "%d(", abs(groups[i]));
    for (k=i; k<j; k++){
      nrvalue+=sprintf(rvalue+nrvalue, "%d ", ports[k]);
    }
    nrvalue+=sprintf(rvalue+nrvalue, "%d) ", ports[j]);
    i=j;
  }
  rvalue[nrvalue-1]=0;
  return rvalue;
}

int ext_mpi_scan_ports_groups(char *str, int **ports, int **groups){
  char *c, *c2, *c3;
  int size=0, i=0, j=0, flag;
  for (c=str; *c; c++){
    if ((*c==' ')||(*c==',')||(*c=='(')||(*c==')')){
      size++;
    }
  }
  size=(size+1)*2;
  *ports=(int*)malloc(size*sizeof(int));
  *groups=(int*)malloc(size*sizeof(int));
  c2=c=strdup(str);
  while ((*c2==' ')||(*c2==',')){
    c2++;
  }
  while (*c2){
    c3=c2;
    while ((*c3!=' ')&&(*c3!=',')&&(*c3!='(')&&(*c3)){
      c3++;
    }
    *c3=0;
    sscanf(c2, "%d", &(*groups)[i]);
    c2=c3+1;
    while ((*c2==' ')||(*c2==',')||(*c2=='(')){
      c2++;
    }
    j=i;
    flag=1;
    while (flag){
      c3=c2;
      while ((*c3!=' ')&&(*c3!=',')&&(*c3!=')')){
        c3++;
      }
      flag=(*c3!=')');
      *c3=0;
      sscanf(c2, "%d", &(*ports)[j]);
      (*groups)[j]=(*groups)[i];
      j++;
      c2=c3+1;
      while ((*c2==' ')||(*c2==',')){
        c2++;
      }
    }
    (*groups)[j-1]*=-1;
    i=j;
    while ((*c2==' ')||(*c2==',')){
      c2++;
    }
  }
  (*ports)[i]=(*groups)[i]=0;
  free(c);
  return 0;
}
