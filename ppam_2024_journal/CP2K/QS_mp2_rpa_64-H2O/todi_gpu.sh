#!/usr/local/bin/bash -l

export LOCAL_RANK=$SLURM_LOCALID
export GLOBAL_RANK=$SLURM_PROCID
# The first 4 ranks get assigned to GPUs
# Subsequent ranks get no GPU
export GPUS=(0 1 2 3)
export NUMA=(0 1 2 3 0 1 2 3)
export NUMA_NODE=${NUMA[$LOCAL_RANK]}

export CUDA_VISIBLE_DEVICES=${GPUS[$SLURM_LOCALID%8]}

if [[ -n "${SLURM_PROCID}" ]]; then
    # Use rank information from SLURM
    MPI_RANK=${SLURM_PROCID}
elif [[ -n "${OMPI_COMM_WORLD_RANK}" ]]; then
    # Use rank information from open MPI
    MPI_RANK=${OMPI_COMM_WORLD_RANK}
else
    echo "sanitizer_wrapper.sh: Can not detect current tasks MPI rank." >&2
    exit 1
fi

ulimit -s unlimited
numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE bash -c "$@"
#numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE bash -c "/user-environment/linux-sles15-neoverse_v2/gcc-12.3.0/nvhpc-24.3-ti5vnjw2lq7oydromjw6bvnb7aliu6qa/Linux_aarch64/24.3/compilers/bin/compute-sanitizer \
#    --tool memcheck \
#    --print-limit 0 \
#    --launch-timeout 0 \
#    $@ > LOG.sanitizer.${SLURM_JOBID}.${MPI_RANK}"
