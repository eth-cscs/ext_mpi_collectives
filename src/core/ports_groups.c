#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
      (*ports)[j]*=-1;
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
