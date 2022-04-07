#include <stdlib.h>
#include <stdio.h>
#include "read_bench.h"
#include "cost_estimation.h"
#include "cost_simulation.h"

int main(){
  int size, num_nodes;
  ext_mpi_read_bench();
  for (num_nodes=160; num_nodes>0; num_nodes--){
    for (size=10000; size<=10000; size*=2){
      ext_mpi_allreduce_simulate(size, 8, num_nodes, 1, 1, 1, 0, 1);
    }
  }
  ext_mpi_delete_bench();
  return 0;
}
