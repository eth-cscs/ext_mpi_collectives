#!/bin/bash

files="slurm-6879.out slurm-6994.out slurm-7016.out slurm-7094.out slurm-7122.out slurm-7158.out slurm-7163.out slurm-7190.out slurm-7216.out slurm-7231.out slurm-7686.out slurm-7766.out slurm-7780.out slurm-7783.out slurm-7785.out"

for item in calls_allreduce size_allreduce timing_allreduce calls_my_allreduce size_my_allreduce calls_reduce_scatter_block size_reduce_scatter_block timing_reduce_scatter_block timing_finalize
do

/bin/rm temp.tmp
for i in $files
do
  cat $i | sed 's/calls /calls_/' | sed 's/size /size_/' | sed 's/timing /timing_/' | grep $item | sed "s/.*$item //" | sed q >> temp.tmp
done
printf "$item HPE_MPI "
awk -F' ' '{sum+=$0;a[NR]=$0}END{for(i in a)y+=(a[i]-(sum/NR))^2;print sum/NR, sqrt(y/(NR-1))}' temp.tmp

/bin/rm temp.tmp
for i in $files
do
  cat $i | sed 's/calls /calls_/' | sed 's/size /size_/' | sed 's/timing /timing_/' | grep $item | sed "s/.*$item //" | sed '$!d' >> temp.tmp
done
printf "$item ext_mpi "
awk -F' ' '{sum+=$0;a[NR]=$0}END{for(i in a)y+=(a[i]-(sum/NR))^2;print sum/NR, sqrt(y/(NR-1))}' temp.tmp

done



# calls allreduce 2.949940e+05
# size allreduce 3.648955e+03
# timing allreduce 1.403075e-05
# calls my_allreduce 2.835470e+05
# size my_allreduce 3.730720e+03
# calls reduce_scatter_block 4.600000e+01
# size reduce_scatter_block 2.097152e+06
# timing reduce_scatter_block 3.900641e-03
# calls allgather 3.500000e+01
# size allgather 2.097152e+06
# timing allgather 8.837537e-03
# calls reduce 2.130700e+04
# size reduce 4.600419e+05
# timing reduce 1.465540e-04
# calls bcast 8.766000e+03
# size bcast 5.297559e+05
# timing bcast 1.550756e-04
# timing finalize 2.759440e+02
