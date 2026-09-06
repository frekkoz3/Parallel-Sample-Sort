#ifndef PARSER_H
#define PARSER_H

#include "utils.h"

void print_usage (char *program_name);
int parse_options (int argc, char **argv, options_t *options);
int validate_options (options_t *options);

#endif /* PARSER_H */