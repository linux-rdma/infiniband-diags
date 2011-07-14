/*
 * Copyright (c) 2011 Lawrence Livermore National Security.  All rights reserved.
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

#ifndef __CHECK_FABRIC_H_
#define __CHECK_FABRIC_H_

int generate_from_fabric(ibnd_fabric_t *fabric, char *generate,
			nn_map_t *name_map, char *ignore_regex,
			int print_missing);

int check_links(ib_portid_t *port_id,
		struct ibmad_port *ibmad_port,
		ibnd_fabric_t *fabric,
		nn_map_t *name_map,
		/* flags */
		int all,
		char *guid_str,
		uint64_t guid,
		char *dr_path,
		char *downnodes_str,
		char *fabricconffile,
		uint16_t sm_lid,
		int print_addr_info);

#endif /* __CHECK_FABRIC_H_ */

