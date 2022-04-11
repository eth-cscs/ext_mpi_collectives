#include <math.h>
#include <stdlib.h>
#include "constants.h"
#include "read_bench.h"
#include "cost_simple_recursive.h"

static double cost_simple_recursive_rec(int p, double n, int fac, int port_max, int *rarray, int num_sockets) {
  double T, T_min, ma, mb;
  int r, i, j, k, tarray[p + 1];
  T_min = 1e60;
  if (port_max > ext_mpi_file_input[ext_mpi_file_input_max - 1].nports) {
    port_max = ext_mpi_file_input[ext_mpi_file_input_max - 1].nports;
  }
  if (port_max > (p - 1) / fac + 1) {
    port_max = (p - 1) / fac + 1;
  }
  if (port_max < 1) {
    port_max = 1;
  }
  for (i = 1; i <= port_max; i++) {
    r = i + 1;
    if ((r - 1) * num_sockets > port_max) r = port_max / num_sockets + 1;
    if (r < 2) r = 2;
    ma = fac;
    if (ma * r > p) {
      ma = (p - ma) / (r - 1);
    }
    mb = n * ma;
    mb /= ext_mpi_file_input[((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].parallel;
    j = floor(mb / (ext_mpi_file_input[1].msize - ext_mpi_file_input[0].msize)) - 1;
    k = j + 1;
    if (j < 0) {
      T = ext_mpi_file_input[0 + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].deltaT;
    } else {
      if (k >= ext_mpi_file_input_max_per_core) {
        T = ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                       ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core]
                .deltaT *
            mb /
            ext_mpi_file_input[ext_mpi_file_input_max_per_core - 1 +
                       ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core]
                .msize;
      } else {
        T = ext_mpi_file_input[j + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].deltaT +
            (mb - ext_mpi_file_input[j + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].msize) *
                (ext_mpi_file_input[k + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].deltaT -
                 ext_mpi_file_input[j + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].deltaT) /
                (ext_mpi_file_input[k + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].msize -
                 ext_mpi_file_input[j + ((r - 1) * num_sockets - 1) * ext_mpi_file_input_max_per_core].msize);
      }
    }
    if (fac * r < p) {
      T += cost_simple_recursive_rec(p, n, fac * r, port_max, tarray + 1, num_sockets);
    } else {
      tarray[1] = 0;
    }
    if (T < T_min) {
      T_min = T;
      tarray[0] = r;
      j = 0;
      while (tarray[j] > 0) {
        rarray[j] = tarray[j];
        j++;
      }
      rarray[j] = 0;
    }
  }
  return (T_min);
}

double ext_mpi_cost_simple_recursive(int p, double n, int port_max, int *num_ports, int *groups, int num_sockets) {
  double cost;
  int i = 0, e = 1;
  p /= num_sockets;
  cost = cost_simple_recursive_rec(p, n * num_sockets, 1, port_max, num_ports, num_sockets);
  while (num_ports[i]) {
    num_ports[i]--;
    groups[i] = p;
    i++;
  }
  groups[i-1]*=-1;
  groups[i] = 0;
  while (e < num_sockets) {
    num_ports[i] = 1;
    groups[i] = -2;
    i++; e *= 2;
    num_ports[i] = groups[i] = 0;
  }
  return cost;
}

/*static void cost_estimated(int p, double n, int port_max, int *rarray){
  double T, T_min, mb;
  int r, i, j, k, l, rr;
  if (port_max>file_input[file_input_max-1].nports){
    port_max = file_input[file_input_max-1].nports;
  }
  if (port_max<1){
    port_max = 1;
  }
  l = 0;
  rr = 1;
  while (rr<p){
    T_min = 1e60;
    for (i=1; i<=port_max; i++){
      r = i+1;
      mb = n*rr;
      mb /= file_input[(r-1-1)*file_input_max_per_core].parallel;
      j = floor(mb/(file_input[1].msize-file_input[0].msize))-1;
      k = j+1;
      if (j<0){
        T = file_input[0+(r-1-1)*file_input_max_per_core].deltaT;
      }else{
        if (k>=file_input_max_per_core){
          T =
file_input[file_input_max_per_core-1+(r-1-1)*file_input_max_per_core].deltaT*mb/file_input[file_input_max_per_core-1+(r-1-1)*file_input_max_per_core].msize;
        }else{
          T =
file_input[j+(r-1-1)*file_input_max_per_core].deltaT+(mb-file_input[j+(r-1-1)*file_input_max_per_core].msize)*(file_input[k+(r-1-1)*file_input_max_per_core].deltaT-file_input[j+(r-1-1)*file_input_max_per_core].deltaT)
                                                                                                                           /(file_input[k+(r-1-1)*file_input_max_per_core].msize-file_input[j+(r-1-1)*file_input_max_per_core].msize);
        }
        T /= r;
      }
      if (T<T_min){
        T_min = T;
        rarray[l] = r;
      }
    }
    rr*=rarray[l];
    l++;
  }
}*/
