#include "optimise_buffers.h"
#include "read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int generate_optimise_buffers(char *buffer_in, char *buffer_out) {
  int nbuffer_out = 0, nbuffer_in = 0, flag, flag2, flag3, o1, o2, size, o1_,
      o2_, size_, partner, num_comm, line2_new, nline2, i;
  char line[1000], line2[1000];
  struct parameters_block *parameters;
  enum eassembler_type estring1, estring2, estring3, estring1_, estring2_,
      estring3_;
  nbuffer_in += read_parameters(buffer_in + nbuffer_in, &parameters);
  nbuffer_out += write_parameters(parameters, buffer_out + nbuffer_out);
  do {
    nbuffer_in += flag3 =
        read_line(buffer_in + nbuffer_in, line, parameters->ascii_in);
    if (flag3) {
      flag = 1;
      if (read_assembler_line_s(line, &estring1, 0) >= 0) {
        if ((estring1 == ememcpy) || (estring1 == ememcp_)) {
          if (read_assembler_line_ssdsdd(line, &estring1, &estring2, &o1,
                                         &estring3, &o2, &size, 0) >= 0) {
            line2_new = ((nline2 = read_line(buffer_in + nbuffer_in, line2,
                                             parameters->ascii_in)) > 0);
            flag2 = 1;
            while (line2_new && flag2) {
              if (read_assembler_line_ssdsdd(line2, &estring1_, &estring2_,
                                             &o1_, &estring3_, &o2_, &size_,
                                             0) >= 0) {
                if ((o1 + size == o1_) && (o2 + size == o2_) &&
                    (estring2_ == eshmemp) && (estring3_ == eshmemp)) {
                  size += size_;
                  i = read_line(buffer_in + nbuffer_in + nline2, line2,
                                parameters->ascii_in);
                  line2_new = (i > 0);
                  nline2 += i;
                } else {
                  flag2 = 0;
                }
              } else {
                if (read_assembler_line_s(line2, &estring1, 0) >= 0) {
                  if (estring1 == enode_barrier) {
                    i = read_line(buffer_in + nbuffer_in + nline2, line2,
                                  parameters->ascii_in);
                    line2_new = (i > 0);
                    nline2 += i;
                    if (line2_new) {
                      if (read_assembler_line_ssdddd(
                              line2, &estring1_, &estring2_, &o1_, &size_,
                              &partner, &num_comm, 0) >= 0) {
                        if (((estring1_ == eisend) || (estring1_ == eisen_)) &&
                            ((o1 == o1_) && (size == size_) &&
                             (estring2_ == eshmemp))) {
                          nbuffer_out += write_assembler_line_ssdddd(
                              buffer_out + nbuffer_out, estring1_, estring2_,
                              o2, size_, partner, num_comm,
                              parameters->ascii_out);
                          nbuffer_in += nline2;
                          flag = 0;
                        }
                      }
                    }
                  }
                }
                flag2 = 0;
              }
            }
          }
        }
      }
      if (flag) {
        nbuffer_out +=
            write_line(buffer_out + nbuffer_out, line, parameters->ascii_out);
      }
    }
  } while (flag3);
  nbuffer_out += write_eof(buffer_out + nbuffer_out, parameters->ascii_out);
  return (nbuffer_out);
}
