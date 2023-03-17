#include "clean_barriers.h"
#include "constants.h"
#include "read_write.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int global_min(int i, MPI_Comm comm_row, MPI_Comm comm_column) {
  PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_column);
  }
  return (i);
}

int ext_mpi_clean_barriers(char *buffer_in, char *buffer_out, MPI_Comm comm_row,
                   MPI_Comm comm_column) {
  int nbuffer_out = 0, nbuffer_in = 0, i, flag, flag2 = 0, integer1, rank;
  char line[1000];
  enum eassembler_type estring1;
  struct parameters_block *parameters;
  MPI_Comm comm_rowl = MPI_COMM_NULL, comm_columnl = MPI_COMM_NULL;
  MPI_Comm_rank(comm_row, &rank);
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  i = global_min(i, comm_row, comm_column);
  if (i < 0)
    goto error;
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  i = 0;
  if (PMPI_Comm_split(comm_row, rank / parameters->socket_row_size,
                      rank % parameters->socket_row_size,
                      &comm_rowl) == MPI_ERR_INTERN)
    i = ERROR_MALLOC;
  if (i < 0)
    goto error;
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_rank(comm_column, &rank);
    i = 0;
    if (PMPI_Comm_split(comm_column, rank / parameters->socket_column_size,
                        rank % parameters->socket_column_size,
                        &comm_columnl) == MPI_ERR_INTERN)
      i = ERROR_MALLOC;
    if (i < 0)
      goto error;
  }
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag) {
      if (ext_mpi_read_assembler_line(line, 0, "sd", &estring1, &integer1) != -1) {
        if (!((estring1 == enop) ||
              ((estring1 == ewaitany) && (integer1 == 0)))) {
          if (estring1 != esocket_barrier) {
            nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line,
                                              parameters->ascii_out);
            flag2 = 0;
          } else {
            PMPI_Allreduce(MPI_IN_PLACE, &flag2, 1, MPI_INT, MPI_MIN, comm_rowl);
            if (comm_column != MPI_COMM_NULL) {
              PMPI_Allreduce(MPI_IN_PLACE, &flag2, 1, MPI_INT, MPI_MIN,
                             comm_columnl);
            }
            if (!flag2) {
              nbuffer_out += ext_mpi_write_line(buffer_out + nbuffer_out, line,
                                                parameters->ascii_out);
              flag2 = 1;
            }
          }
        }
      }
    }
  } while (flag);
  ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  if (comm_columnl != MPI_COMM_NULL) {
    PMPI_Comm_free(&comm_columnl);
  }
  if (comm_rowl != MPI_COMM_NULL) {
    PMPI_Comm_free(&comm_rowl);
  }
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
error:
  if (comm_columnl != MPI_COMM_NULL) {
    PMPI_Comm_free(&comm_columnl);
  }
  if (comm_rowl != MPI_COMM_NULL) {
    PMPI_Comm_free(&comm_rowl);
  }
  ext_mpi_delete_parameters(parameters);
  return ERROR_MALLOC;
}
