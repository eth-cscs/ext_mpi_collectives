#!/bin/bash -l

set -x

my_collectives=/scratch/snx2000/ajocksch/my_collectives.git
num_cores=12

for name in allreduce reduce_scatter allgatherv reduce_scatter_nms allgatherv_nms reduce gatherv scatterv bcast
do

    cc -O2 -g -I. -DNUM_CORES=$num_cores -DEXT_MPI -I$my_collectives/include -L$my_collectives/lib osu_$name.c osu_util.c -lext_mpi_collectives -lm -lrt -o my_$name
    /scratch/snx2000/ajocksch/openmpi-4.1.0/bin/mpicc -O2 -g -I. -DNUM_CORES=$num_cores -DEXT_MPI -I/scratch/snx2000/ajocksch/openmpi/include -I$my_collectives/include -L$my_collectives/lib osu_$name.c osu_util.c -lext_mpi_collectives_ompi -lm -lrt -o my_ompi_$name

#    cc -O2 -g -I. -DNUM_CORES=$num_cores -DEXT_NOPER -I$my_collectives/include -L$my_collectives/lib osu_$name.c osu_util.c -lext_mpi_collectives -lm -lrt -o my_np_$name
#    /scratch/snx2000/ajocksch/openmpi/bin/mpicc -O2 -g -I. -DNUM_CORES=$num_cores -DEXT_NOPER -I/scratch/snx2000/ajocksch/openmpi/include -I$my_collectives/include -L$my_collectives/lib osu_$name.c osu_util.c -lext_mpi_collectives_ompi -lm -lrt -o my_ompi_np_$name

    cc -O2 -g -I. -DNUM_CORES=$num_cores -I$my_collectives/include -L$my_collectives/lib osu_$name.c osu_util.c -lext_mpi_collectives -lm -lrt -o ref_$name
#    /scratch/snx2000/ajocksch/openmpi/bin/mpicc -O2 -g -I. -DNUM_CORES=$num_cores -I/scratch/snx2000/ajocksch/openmpi/include -I$my_collectives/include -L$my_collectives/lib osu_$name.c osu_util.c -lext_mpi_collectives_ompi -lm -lrt -o ref_ompi_$name

#    cc -O2 -g -I. -DNUM_CORES=$num_cores -DPERSISTENT -I$my_collectives/include -L$my_collectives/lib osu_$name.c osu_util.c -lext_mpi_collectives -lm -lrt -o persistent_$name
    /scratch/snx2000/ajocksch/openmpi-4.1.0/bin/mpicc -O2 -g -I. -DNUM_CORES=$num_cores -DPERSISTENT -I/scratch/snx2000/ajocksch/openmpi/include -I$my_collectives/include -L$my_collectives/lib osu_$name.c osu_util.c -lext_mpi_collectives_ompi -lm -lrt -o persistent_ompi_$name

done
