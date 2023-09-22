# Copyright (c) 2023 CSCS
# BSD 3-Clause License

from spack.package import *


class ExtMpiCollectives(CMakePackage, CudaPackage):
    """
    Dr Jocksch persistent collective communication library:
    spack install ext_mpi_collectives +cuda +tests # cuda_arch=80
    # TODO: release tag
    # TODO: CUDA_ARCHITECTURES
    # TODO: node_size_threshold.tmp and latency_bandwidth.tmp
    """

    homepage = "https://github.com/eth-cscs/ext_mpi_collectives"
    git = "https://github.com/eth-cscs/ext_mpi_collectives.git"
    maintainers = ["ajocksch", "jgphpc"]
    version("master", branch="master")

    # patch("0.patch", sha256="0d24fcc660ebad6fe48e3f7c35697c781b499c5d3b3e82ad131c2911bde157e1")
    variant("cuda", default=False, description="Build with CUDA")
    variant("tests", default=False, description="Build with tests")
    depends_on("cmake@3.25.0:")
    depends_on("mpi") # , when="+mpi")
    depends_on("cuda@11:", when="+cuda")
#     TODO: see CUDA_ARCHITECTURES below
#     conflicts(
#         "cuda_arch=none",
#         when="+cuda",
#         msg="A value for cuda_arch must be specified. Add cuda_arch=XX",
#     )

    def cmake_args(self):
        spec = self.spec
        args = []
        # args.append("-DCMAKE_VERBOSE_MAKEFILE=ON")
        if "+tests" in spec:
            args.append("-DBUILD_TESTING=ON")

#         TODO: remove CUDA_ARCHITECTURES "60;80" from src/gpu/CMakeLists.txt
#         if "+cuda" in spec:
#             # args.append(self.define("CMAKE_CUDA_HOST_COMPILER", self.spec["mpi"].mpicxx))
#             if not spec.satisfies("cuda_arch=none"):
#                 cuda_arch = spec.variants["cuda_arch"].value
#                 args.append(f"-DCMAKE_CUDA_ARCHITECTURES={cuda_arch[0]}")

        return args
