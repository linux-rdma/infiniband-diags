/*
 * Copyright (c) 2011 Lawrence Livermore National Security All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <iba/ib_types.h>
#include <infiniband/ibfabricconf.h>

char *fabricconf_file = IBFC_DEF_CONFIG;
char *argv0 = NULL;

char *delim_out = NULL;

void
print_port(ibfc_port_t *port, void *user_data)
{
	char prop[256];
	char port_ext_num_str[8];
	char rport_ext_num_str[8];
	int i = 0;

	port_ext_num_str[0] = '\0';
	rport_ext_num_str[0] = '\0';

	if ((i = ibfc_port_get_port_ext_num(port)) != 0)
		sprintf(port_ext_num_str, "%3d", i);
	if ((i = ibfc_port_get_port_ext_num(ibfc_port_get_remote(port))) != 0)
		sprintf(rport_ext_num_str, "%3d", i);

	if (delim_out)
		printf("%s%s%d%s%s%s%d%s%s%s%s%s%s%s%s\n",
			ibfc_port_get_name(port),
			delim_out,
			ibfc_port_get_port_num(port),
			delim_out,
			port_ext_num_str,
			delim_out,
			ibfc_port_get_port_num(ibfc_port_get_remote(port)),
			delim_out,
			rport_ext_num_str,
			delim_out,
			ibfc_port_get_name(ibfc_port_get_remote(port)),
			delim_out,
			ibfc_speed_str(ibfc_port_get_speed(port)),
			delim_out,
			ibfc_width_str(ibfc_port_get_width(port)));
	else
		printf ("\"%s\" p:%3d[%s]  <==(%s)==>  p:%3d[%s] \"%s\"\n",
			ibfc_port_get_name(port),
			ibfc_port_get_port_num(port),
			port_ext_num_str,
			ibfc_sprintf_port_properties(prop, 256, port),
			ibfc_port_get_port_num(ibfc_port_get_remote(port)),
			rport_ext_num_str,
			ibfc_port_get_name(ibfc_port_get_remote(port))
			);
}

/** =========================================================================
 */
static int
usage(void)
{
        fprintf(stderr,
"%s [options] [node] [port]\n"
"Usage: parse the fabricconf file\n"
"\n"
"Options:\n"
"  --config, -c <ibfabricconf> Use an alternate fabric config file (default: %s)\n"
"  --check_dup print duplicate entries and quit\n"
"  --delim_out, -d <deliminator> output colums deliminated by <deliminator>\n"
"  [node] if node is specified print ports for that node\n"
"  [port] if port is specified print information for just that port (default \"all\")\n"
"         if neither node nor port is specified print entire config file\n"
"\n"
, argv0,
IBFC_DEF_CONFIG
);
        return (0);
}


int
main(int argc, char **argv)
{
	ibfc_conf_t *fabricconf;
	int rc = 0;
	int check_dup = 0;
        char  ch = 0;
        static char const str_opts[] = "hc:wd:";
        static const struct option long_opts [] = {
           {"help", 0, 0, 'h'},
           {"config", 1, 0, 'c'},
           {"check_dup", 0, 0, 1},
           {"delim_out", 1, 0, 'd'},
	   {0, 0, 0, 0}
        };

	argv0 = argv[0];

        while ((ch = getopt_long(argc, argv, str_opts, long_opts, NULL))
                != -1)
        {
                switch (ch)
                {
			case 'c':
				fabricconf_file = strdup(optarg);
				break;
			case 1:
				check_dup = 1;
				break;
			case 'd':
				delim_out = strdup(optarg);
				break;
                        case 'h':
                        default:
				exit(usage());
                }
	}

	argc -= optind;
	argv += optind;

	fabricconf = ibfc_alloc_conf();
	if (!fabricconf) {
		fprintf(stderr, "ERROR: Failed to alloc fabricconf\n");
		exit(1);
	}

	if (check_dup)
		ibfc_set_stderr(fabricconf, stdout);

	ibfc_set_warn_dup(fabricconf, check_dup);
	rc = ibfc_parse_file(fabricconf_file, fabricconf);

	if (rc) {
		fprintf(stderr, "ERROR: failed to parse fabric config "
			"\"%s\": %s\n", fabricconf_file, strerror(rc));
		return (rc);
	}

	if (check_dup)
		goto done;

	if (delim_out) {
		printf("Fabric Name%s%s\n", delim_out, ibfc_conf_get_name(fabricconf));
		printf("Node%sPort%sRem Port%sRem Node%sSpeed%sWidth\n",
			delim_out, delim_out, delim_out, delim_out, delim_out);
	} else
		printf("Fabric Name: %s\n", ibfc_conf_get_name(fabricconf));

	if (argv[0]) {
		char prop[256];
		int p_num = 1;
		if (argv[1]) {
			p_num = strtol(argv[1], NULL, 0);
			ibfc_port_t *port = ibfc_get_port(fabricconf, argv[0], p_num);
			if (port)
				print_port(port, NULL);
			else
				fprintf (stderr, "ERROR: \"%s\":%d port not found\n",
					argv[0], p_num);
		} else {
			ibfc_port_list_t *port_list;
			int rc = ibfc_get_port_list(fabricconf, argv[0],
							&port_list);
			if (rc) {
				fprintf(stderr, "ERROR: Failed to get port "
					"list for \"%s\":%s\n",
					argv[0], strerror(rc));
			} else {
				ibfc_iter_port_list(port_list, print_port,
							NULL);
				ibfc_free_port_list(port_list);
			}
		}
	} else {
		ibfc_iter_ports(fabricconf, print_port, NULL);
	}

done:
	ibfc_free(fabricconf);

	return (rc);
}

