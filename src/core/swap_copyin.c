#include "swap_copyin.h"
#include "read_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_mpi_generate_swap_copyin(char *buffer_in, char *buffer_out) {
  struct line_irecv_isend data_irecv_isend;
  int nbuffer_out = 0, nbuffer_in = 0, flag, i;
  char line[1000], **lines_recv, **lines_send;
  struct parameters_block *parameters;
  nbuffer_in += ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += ext_mpi_write_parameters(parameters, buffer_out + nbuffer_out);
  nbuffer_in += flag =
      ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
  lines_recv = (char **)malloc(abs(parameters->num_ports[0]) * sizeof(char *));
  lines_send = (char **)malloc(abs(parameters->num_ports[0]) * sizeof(char *));
  for (i = 0; i < abs(parameters->num_ports[0]); i++) {
    lines_recv[i] = (char *)malloc(1000 * sizeof(char *));
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, lines_recv[i], parameters->ascii_in);
  }
  for (i = 0; i < abs(parameters->num_ports[0]); i++) {
    lines_send[i] = (char *)malloc(1000 * sizeof(char *));
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, lines_send[i], parameters->ascii_in);
  }
  for (i = 0; i < abs(parameters->num_ports[0]); i++) {
    nbuffer_out +=
        ext_mpi_write_line(buffer_out + nbuffer_out, lines_recv[i], parameters->ascii_out);
  }
  for (i = 0; i < abs(parameters->num_ports[0]); i++) {
    ext_mpi_read_irecv_isend(lines_send[i], &data_irecv_isend);
    data_irecv_isend.buffer_type = esendbufp;
    data_irecv_isend.offset = 0;
    nbuffer_out +=
	ext_mpi_write_irecv_isend(buffer_out + nbuffer_out, &data_irecv_isend, parameters->ascii_out);
  }
  nbuffer_out +=
      ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
  do {
    nbuffer_in += flag =
        ext_mpi_read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag) {
      nbuffer_out +=
          ext_mpi_write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
    }
  } while (flag);
  for (i = 0; i < abs(parameters->num_ports[0]); i++) {
    free(lines_send[i]);
    free(lines_recv[i]);
  }
  free(lines_send);
  free(lines_recv);
  ext_mpi_write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  ext_mpi_delete_parameters(parameters);
  return nbuffer_out;
}
