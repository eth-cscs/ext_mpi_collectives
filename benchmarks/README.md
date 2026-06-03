#Simple benchmark for ext_mpi

The benchmark compares blocking MPI_Allreduce communication with persistent MPI_Allreduce_init, ... communication. It should be run without and with the ext_mpi library. The setup is for 128 MPI tasks on one shared memory node. For other configurations the source code needs to be adapted.

For a reference run execute

```
mpicc benchmark1.c -lm
srun -N 1 --ntasks-per-node=128 ./a.out
```

For a run with the ext_mpi library recompile and set the environment variable EXT_MPI_BLOCKING if desired

```
mpicc benchmark1.c -L../lib/ -lext_mpi_collectives -lxpmem -lm
EXT_MPI_BLOCKING=1 srun -N 1 --ntasks-per-node=128 ./a.out
```
