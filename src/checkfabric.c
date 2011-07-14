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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <inttypes.h>
#include <regex.h>

#include <glib.h>

/* global IB headers */
#include <complib/cl_nodenamemap.h>
#include <infiniband/ibnetdisc.h>
#include <infiniband/ibfabricconf.h>

/* local headers */
#include "ibdiag_common.h"
#include "checkfabric.h"

#define SCHEMA_VERSION "1.0"

typedef struct {
	uint64_t guid;
} guid_list_item_t;
typedef struct {
	FILE *fp;
	char *ignore_regex;
	int print_missing;
	GSList * visited_nodes; /* list guid_list_item_t */
	int vn_cnt;
} print_node_data_t;

/* list of node name strings */
typedef struct nodelist
{
	int cnt;
	GSList *list;
} nodelist_t;

/* Globals */
static nn_map_t *node_name_map;
static int smlid = 0;
static ibfc_conf_t *fabricconf = NULL;
static nodelist_t *downnodes;
static char *fabric_name = "fabric";
static int check_node_rc = 0;
static int print_port_guids = 0;
static int print_addr_info = 0;

static struct {
	int num_ports;
	int pn_down;
	int pn_init;
	int pn_armed;
	int pn_active;
	int pn_disabled;
	int pn_sdr;
	int pn_ddr;
	int pn_qdr;
	int pn_fdr10;
	int pn_fdr;
	int pn_edr;
	int pn_1x;
	int pn_4x;
	int pn_8x;
	int pn_12x;
	int pn_undef;
} totals = {
	num_ports   : 0,
	pn_down     : 0,
	pn_init     : 0,
	pn_armed    : 0,
	pn_active   : 0,
	pn_disabled : 0,
	pn_sdr      : 0,
	pn_ddr      : 0,
	pn_qdr      : 0,
	pn_fdr10    : 0,
	pn_fdr      : 0,
	pn_edr      : 0,
	pn_1x       : 0,
	pn_4x       : 0,
	pn_8x       : 0,
	pn_12x      : 0,
	pn_undef    : 0
};

typedef struct port_vis {
	struct port_vis *next;
	uint64_t guid;
	int pnum;
} port_vis_t;

port_vis_t *vis_head = NULL;

static nodelist_t * nodelist_create(char *downnodes_str)
{
	char *last, *tok;
	char *tmp_str = strdup(downnodes_str);
	nodelist_t *rc = (nodelist_t *)calloc(1, sizeof(*rc));

	if (!rc)
		return (NULL);

	tok = strtok_r(tmp_str, ",", &last);
	while (tok) {
		gpointer tmp = (gpointer)strdup(tok);
		rc->list = g_slist_append(rc->list, tmp);
		rc->cnt++;
		tok = strtok_r(NULL, ",", &last);
	}

	free(tmp_str);
	return (rc);
}

static void free_node(gpointer elem, gpointer user)
{
	free(elem);
}

static void nodelist_destroy(nodelist_t *nodelist)
{
	if (!nodelist)
		return;

	g_slist_foreach(nodelist->list, free_node, NULL);
	g_slist_free(nodelist->list);
	free(nodelist);
}

static int nodelist_find(nodelist_t *nodelist, char *target)
{
	int i = 0;
	for (i = 0; i < nodelist->cnt; i++) {
		gpointer t = g_slist_nth_data(nodelist->list, i);
		if (t && strcmp((const char *)t, (const char *)target) == 0)
			return (1);
	}

	return (0);
}


static void mark_port_seen(uint64_t guid, int pnum)
{
	port_vis_t *tmp = (port_vis_t *)calloc(1, sizeof *tmp);
	if (!tmp) {
		fprintf(stderr, "calloc failure\n");
		exit(1);
	}
	tmp->guid = guid;
	tmp->pnum = pnum;
	tmp->next = vis_head;
	vis_head = tmp;
}

static int port_seen(uint64_t guid, int pnum)
{
	port_vis_t *cur;
	for (cur = vis_head; cur; cur = cur->next) {
		if (guid == cur->guid && pnum == cur->pnum)
			return (1);
	}
	return (0);
}

static void free_seen(void)
{
	port_vis_t *cur = vis_head;
	while (cur) {
		port_vis_t *tmp = cur;
		cur = cur->next;
		free(tmp);
	}
}

void
print_port_stats(void)
{
	printf("\nStats Summary: (%d total physical ports)\n", totals.num_ports);
	if (totals.pn_down)
		printf("   %d down ports(s)\n", totals.pn_down);
	if (totals.pn_disabled)
		printf("   %d disabled ports(s)\n", totals.pn_disabled);
	if (totals.pn_1x)
		printf("   %d link(s) at 1X\n", totals.pn_1x);
	if (totals.pn_4x)
		printf("   %d link(s) at 4X\n", totals.pn_4x);
	if (totals.pn_8x)
		printf("   %d link(s) at 8X\n", totals.pn_8x);
	if (totals.pn_12x)
		printf("   %d link(s) at 12X\n", totals.pn_12x);
	if (totals.pn_sdr)
		printf("   %d link(s) at 2.5 Gbps (SDR)\n", totals.pn_sdr);
	if (totals.pn_ddr)
		printf("   %d link(s) at 5.0 Gbps (DDR)\n", totals.pn_ddr);
	if (totals.pn_qdr)
		printf("   %d link(s) at 10.0 Gbps (QDR)\n", totals.pn_qdr);
	if (totals.pn_fdr10)
		printf("   %d link(s) at 10.0+ Gbps (FDR10)\n", totals.pn_fdr10);
	if (totals.pn_fdr)
		printf("   %d link(s) at 14.0 Gbps (FDR)\n", totals.pn_fdr);
	if (totals.pn_edr)
		printf("   %d link(s) at 25.0 Gbps (EDR)\n", totals.pn_edr);
}


static void cf_print_port(char *node_name, ibnd_node_t * node, ibnd_port_t * port,
		   ibfc_port_t *portconf, int inc_attributes)
{
	char width[64], speed[64], state[64], physstate[64];
	char remote_addr_str[256];
	char remote_str[256];
	char link_str[256];
	char width_msg[256];
	char speed_msg[256];
	char ext_port_str[256];
	int iwidth, ispeed, istate, iphystate, fdr10, espeed, cap_mask;
	int n = 0;
	uint8_t *info;

	if (!port)
		return;

	iwidth = mad_get_field(port->info, 0, IB_PORT_LINK_WIDTH_ACTIVE_F);
	ispeed = mad_get_field(port->info, 0, IB_PORT_LINK_SPEED_ACTIVE_F);
	fdr10 = mad_get_field(port->ext_info, 0,
			      IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F) & FDR10;

	if (port->node->type == IB_NODE_SWITCH)
		info = (uint8_t *)&port->node->ports[0]->info;
	else
		info = (uint8_t *)&port->info;
	cap_mask = mad_get_field(info, 0, IB_PORT_CAPMASK_F);
	if (cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS))
		espeed = mad_get_field(port->info, 0,
				       IB_PORT_LINK_SPEED_EXT_ACTIVE_F);
	else
		espeed = 0;

	istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	iphystate = mad_get_field(port->info, 0, IB_PORT_PHYS_STATE_F);

	remote_addr_str[0] = '\0';
	remote_str[0] = '\0';
	link_str[0] = '\0';
	width_msg[0] = '\0';
	speed_msg[0] = '\0';

	if (inc_attributes)
	{
		/* C14-24.2.1 states that a down port allows for invalid data to be
		 * returned for all PortInfo components except PortState and
		 * PortPhysicalState */
		if (istate != IB_LINK_DOWN) {
			if (!espeed) {
				if (fdr10)
					sprintf(speed, "10.0 Gbps (FDR10)");
				else
					mad_dump_val(IB_PORT_LINK_SPEED_ACTIVE_F, speed,
							64, &ispeed);
			} else
				mad_dump_val(IB_PORT_LINK_SPEED_EXT_ACTIVE_F, speed,
						64, &espeed);

			n = snprintf(link_str, 256, "(%s %s %6s/%8s)",
			     mad_dump_val(IB_PORT_LINK_WIDTH_ACTIVE_F, width, 64,
					  &iwidth),
			     speed,
			     mad_dump_val(IB_PORT_STATE_F, state, 64, &istate),
			     mad_dump_val(IB_PORT_PHYS_STATE_F, physstate, 64,
					  &iphystate));
		} else {
			n = snprintf(link_str, 256, "(%6s/%8s)",
			     mad_dump_val(IB_PORT_STATE_F, state, 64, &istate),
			     mad_dump_val(IB_PORT_PHYS_STATE_F, physstate, 64,
					  &iphystate));
		}
	}

	if (port->remoteport) {
		char *remap =
		    remap_node_name(node_name_map, port->remoteport->node->guid,
				    port->remoteport->node->nodedesc);

		if (port->remoteport->ext_portnum)
			snprintf(ext_port_str, 256, "%d",
				 port->remoteport->ext_portnum);
		else
			ext_port_str[0] = '\0';

		get_max_msg(width_msg, speed_msg, 256, port);

		if (print_addr_info) {
			if (print_port_guids)
				snprintf(remote_addr_str, 256,
					" (0x%016" PRIx64 " %d)",
					port->remoteport->guid,
					port->remoteport->base_lid ?
						port->remoteport->base_lid :
						port->remoteport->node->smalid);
			else
				snprintf(remote_addr_str, 256,
					" (0x%016" PRIx64 " %d)",
					port->remoteport->node->guid,
					port->remoteport->base_lid ?
						port->remoteport->base_lid :
						port->remoteport->node->smalid);
		}

		snprintf(remote_str, 256,
			 "p:%3d[%3s] \"%s\"%s (%s %s)\n",
			 port->remoteport->portnum, ext_port_str, remap,
			 remote_addr_str,
			 width_msg, speed_msg);
		free(remap);
	} else if (portconf) {
		char str[8];
		int ext = ibfc_port_get_port_ext_num(portconf);
		str[0] = '\0';
		if (ext)
			snprintf(str, 8, "%3d", ext);
		snprintf(remote_str, 256,
			 "p:%3d[%3s] \"%s\" (Should be: %s,%s,Active)\n",
				ibfc_port_get_port_num(portconf),
				str,
				ibfc_port_get_name(portconf),
				ibfc_width_str(ibfc_port_get_width(portconf)),
				ibfc_speed_str(ibfc_port_get_speed(portconf)));
	} else
		snprintf(remote_str, 256, " [  ] \"\" ( )\n");

	if (port->ext_portnum)
		snprintf(ext_port_str, 256, "%d", port->ext_portnum);
	else
		ext_port_str[0] = '\0';

	if (print_addr_info)
		printf("(0x%016" PRIx64 " %d) ",
			print_port_guids ? port->guid : node->guid,
			port->base_lid ?  port->base_lid : port->node->smalid);

	printf ("\"%s\" ", node_name);
	if (link_str[0] != '\0')
		printf("p:%3d[%3s] <==%s==>  %s",
			port->portnum, ext_port_str, link_str, remote_str);
	else
		printf("p:%3d[%3s] <==>  %s",
			port->portnum, ext_port_str, remote_str);
}

void
print_config_port(ibfc_port_t *port)
{
	char str[8];
	int ext_num;

	str[0]='\0';
	ext_num = ibfc_port_get_port_ext_num(port);
	if (ext_num)
		snprintf(str, 8, "%3d", ext_num);

	printf ("\"%s\" p:%3d[%3s] <==>  ",
		ibfc_port_get_name(port),
		ibfc_port_get_port_num(port),
		str);

	str[0]='\0';
	ext_num = ibfc_port_get_port_ext_num(ibfc_port_get_remote(port));
	if (ext_num)
		snprintf(str, 8, "%3d", ext_num);

	printf ("p:%3d[%3s] ",
		ibfc_port_get_port_num(ibfc_port_get_remote(port)),
		str);

	printf ("\"%s\"\n",
		ibfc_port_get_name(ibfc_port_get_remote(port)));
}

/* check the port width against the configured width */
int invalid_width(ibnd_port_t *port, ibfc_width_t conf_width)
{
	uint32_t act_width = mad_get_field(port->info, 0, IB_PORT_LINK_WIDTH_ACTIVE_F);

	switch (conf_width)
	{
		case IBFC_WIDTH_MAX:
		{
			uint32_t max_width = get_max(mad_get_field(port->info, 0,
							   IB_PORT_LINK_WIDTH_SUPPORTED_F)
					     & mad_get_field(port->remoteport->info, 0,
							     IB_PORT_LINK_WIDTH_SUPPORTED_F));

			return ((max_width & mad_get_field(port->info, 0,
				IB_PORT_LINK_WIDTH_ACTIVE_F)) == 0);
		}
		case IBFC_1X:
			return (act_width == IB_LINK_WIDTH_ACTIVE_1X);
		case IBFC_4X:
			return (act_width == IB_LINK_WIDTH_ACTIVE_4X);
		case IBFC_8X:
			return (act_width == IB_LINK_WIDTH_ACTIVE_8X);
		case IBFC_12X:
			return (act_width == IB_LINK_WIDTH_ACTIVE_12X);
	}

	return (1);
}

int speed_not_max(ibnd_port_t *port)
{
	uint32_t max_speed = 0;
	uint32_t cap_mask, rem_cap_mask, fdr10;
	uint8_t *info;

	/* this seems way too complicated but "it is what it is".
	 * FIXME
	 * Get this into common code to be used with the get_max_msg code.
	 */
	if (port->node->type == IB_NODE_SWITCH)
		info = (uint8_t *)&port->node->ports[0]->info;
	else
		info = (uint8_t *)&port->info;
	cap_mask = mad_get_field(info, 0, IB_PORT_CAPMASK_F);

	if (port->remoteport->node->type == IB_NODE_SWITCH)
		info = (uint8_t *)&port->remoteport->node->ports[0]->info;
	else
		info = (uint8_t *)&port->remoteport->info;
	rem_cap_mask = mad_get_field(info, 0, IB_PORT_CAPMASK_F);
	if (cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS) &&
	    rem_cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS))
		goto check_ext_speed;
check_fdr10_supp:
	fdr10 = (mad_get_field(port->ext_info, 0,
			       IB_MLNX_EXT_PORT_LINK_SPEED_SUPPORTED_F) & FDR10)
		&& (mad_get_field(port->remoteport->ext_info, 0,
				  IB_MLNX_EXT_PORT_LINK_SPEED_SUPPORTED_F) & FDR10);
	if (fdr10)
		goto check_fdr10_active;

	max_speed = get_max(mad_get_field(port->info, 0,
					  IB_PORT_LINK_SPEED_SUPPORTED_F)
			    & mad_get_field(port->remoteport->info, 0,
					    IB_PORT_LINK_SPEED_SUPPORTED_F));
	return ((max_speed & mad_get_field(port->info, 0,
				       IB_PORT_LINK_SPEED_ACTIVE_F)) == 0);

check_ext_speed:
	if (mad_get_field(port->info, 0,
			  IB_PORT_LINK_SPEED_EXT_SUPPORTED_F) == 0 ||
	    mad_get_field(port->remoteport->info, 0,
			  IB_PORT_LINK_SPEED_EXT_SUPPORTED_F) == 0)
		goto check_fdr10_supp;
	max_speed = get_max(mad_get_field(port->info, 0,
					  IB_PORT_LINK_SPEED_EXT_SUPPORTED_F)
			    & mad_get_field(port->remoteport->info, 0,
					    IB_PORT_LINK_SPEED_EXT_SUPPORTED_F));
	return ((max_speed & mad_get_field(port->info, 0,
				       IB_PORT_LINK_SPEED_EXT_ACTIVE_F)) == 0);

check_fdr10_active:
	return ((mad_get_field(port->ext_info, 0,
		IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F) & FDR10) == 0);
}

/* check the port speed against the configured speed */
int invalid_speed(ibnd_port_t *port, ibfc_speed_t conf_speed)
{
	if (conf_speed == IBFC_SPEED_MAX)
		return (speed_not_max(port));
	else {
		uint32_t speed_act = mad_get_field(port->info, 0,
						IB_PORT_LINK_SPEED_ACTIVE_F);
		uint32_t ext_speed_sup = mad_get_field(port->info, 0,
				    IB_PORT_LINK_SPEED_EXT_SUPPORTED_F);
		uint32_t ext_speed_act = 0;

		uint32_t fdr10_sup = mad_get_field(port->ext_info, 0,
				    IB_MLNX_EXT_PORT_LINK_SPEED_SUPPORTED_F)
				    & FDR10;
		uint32_t fdr10_act = 0;

		if (fdr10_sup)
			fdr10_act = mad_get_field(port->ext_info, 0,
					IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F)
					& FDR10;

		if (ext_speed_sup)
			ext_speed_act = mad_get_field(port->info, 0,
						IB_PORT_LINK_SPEED_EXT_ACTIVE_F);


		switch (conf_speed)
		{
			case IBFC_SDR:
				return (speed_act != IB_LINK_SPEED_ACTIVE_2_5);
			case IBFC_DDR:
				return (speed_act != IB_LINK_SPEED_ACTIVE_5);
			case IBFC_QDR:
				/* the QDR bit is over loaded when Ext Speeds
				 * are active.  If either type of Ext Speeds
				 * are supported and active then the QDR bit is
				 * not the active speed.
				 */
				if ((ext_speed_sup && ext_speed_act)
				    ||
				    (fdr10_sup && fdr10_act))
					return (1);
				return (speed_act != IB_LINK_SPEED_ACTIVE_10);
			case IBFC_FDR10:
				return (!fdr10_sup || !fdr10_act);
			case IBFC_FDR:
				return (!ext_speed_sup ||
					(ext_speed_act !=
					IB_LINK_SPEED_EXT_ACTIVE_14));
			case IBFC_EDR:
				return (!ext_speed_sup ||
					(ext_speed_act !=
					IB_LINK_SPEED_EXT_ACTIVE_25));
			case IBFC_SPEED_MAX:
				break;
		}
	}
	return (0);
}

void compare_port(ibfc_port_t *portconf, char *node_name, ibnd_node_t *node, ibnd_port_t *port)
{
	int istate, iphysstate;

	istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	iphysstate = mad_get_field(port->info, 0, IB_PORT_PHYS_STATE_F);

	ibfc_port_t *rem_portconf = ibfc_port_get_remote(portconf);
	char *rem_node_name = ibfc_port_get_name(rem_portconf);

	if (istate != IB_LINK_ACTIVE) {
		int print = 0;
		int hostdown = 0;

		if (downnodes &&
		    nodelist_find(downnodes, rem_node_name)) {
			hostdown = 1;
		}

		if (iphysstate == IB_PORT_PHYS_STATE_DISABLED) {
			printf("ERR: port disabled");
			if (downnodes && !hostdown)
				printf(" (host UP): ");
			else
				printf(": ");
			print = 1;
		} else {
			if (!downnodes || !hostdown) {
				printf("ERR: port down: ");
				print = 1;
			}
		}
		if (print) {
			cf_print_port(node_name, node, port, rem_portconf, 0);
			check_node_rc = 1;
		}
	} else {
		ibfc_width_t conf_width = ibfc_port_get_width(portconf);
		ibfc_speed_t conf_speed = ibfc_port_get_speed(portconf);
		int rem_port_num = ibfc_port_get_port_num(rem_portconf);
		char *rem_remap = NULL;
		ibnd_port_t *remport = port->remoteport;

		if (remport) {
			if (invalid_width(port, conf_width))
			{
				printf("ERR: width != %s: ",
					ibfc_width_str(conf_width));
				cf_print_port(node_name, node, port, NULL, 1);
				check_node_rc = 1;
			}
			if (invalid_speed(port, conf_speed))
			{
				printf("ERR: speed != %s: ",
					ibfc_speed_str(conf_speed));
				cf_print_port(node_name, node, port, NULL, 1);
				check_node_rc = 1;
			}

			rem_remap = remap_node_name(node_name_map,
					port->remoteport->node->guid,
					port->remoteport->node->nodedesc);
			if (strcmp(rem_node_name, rem_remap) != 0
			    ||
			    rem_port_num != port->remoteport->portnum) {
				printf("ERR: invalid link : ");
				cf_print_port(node_name, node, port, NULL, 0);
				printf("     Should be    : ");
				print_config_port(portconf);
				check_node_rc = 1;
			}
			free(rem_remap);
		} else {
			printf("ERR: query failure: ");
			cf_print_port(node_name, node, port, rem_portconf, 1);
			check_node_rc = 1;
		}
	}
}

/**
 * Right now, this checks only for disabled ports and ports which are active
 * but not at max speed/width.  Without a config file we can't check as much.
 */
void check_basic_config(char *node_name, ibnd_node_t *node, ibnd_port_t *port)
{
	int istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	int iphysstate = mad_get_field(port->info, 0, IB_PORT_PHYS_STATE_F);

	if (iphysstate == IB_PORT_PHYS_STATE_DISABLED) {
		printf("WARNING: Disabled Link: ");
		cf_print_port(node_name, node, port, NULL, 1);
		check_node_rc = 1;
	}

	if (istate == IB_LINK_ACTIVE) {
		if (!port->remoteport) {
			printf("WARNING: Active port with unresponsive remote: ");
			cf_print_port(node_name, node, port, NULL, 1);
			check_node_rc = 1;
		} else if (speed_not_max(port)) {
			printf("WARNING: Slow Link: ");
			cf_print_port(node_name, node, port, NULL, 1);
			check_node_rc = 1;
		}
	}
}

void check_config(char *node_name, ibnd_node_t *node, ibnd_port_t *port)
{
	int istate;
	ibfc_port_t *portconf = NULL;

	portconf = ibfc_get_port(fabricconf, node_name, port->portnum);
	istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	if (portconf) {
		compare_port(portconf, node_name, node, port);
	} else if (istate == IB_LINK_ACTIVE) {
		char *remote_name = NULL;
		ibnd_node_t *remnode;
		ibnd_port_t *remport = port->remoteport;
		if (!remport) {
			printf("ERROR: ibnd error; port ACTIVE "
				"but no remote port! (Lights on, "
				"nobody home?): ");
			cf_print_port(node_name, node, port, NULL, 1);
			check_node_rc = 1;
			goto invalid_active;
		}
		remnode = remport->node;
		remote_name = remap_node_name(node_name_map, remnode->guid,
					remnode->nodedesc);
		portconf = ibfc_get_port(fabricconf, remote_name, remport->portnum);
		if (portconf) {
			compare_port(portconf, remote_name, remnode, remport);
		} else {
invalid_active:
			printf("ERR: Unconfigured active link: ");
			cf_print_port(node_name, node, port, NULL, 1);
			check_node_rc = 1;
		}
		free(remote_name); /* OK; may be null */
	}
}

void check_port(char *node_name, ibnd_node_t * node, ibnd_port_t * port)
{
	int iwidth, ispeed, istate, iphysstate, espeed, fdr10, cap_mask;
	int n_undef = totals.pn_undef;
	uint8_t *info;

	totals.num_ports++;

	iwidth = mad_get_field(port->info, 0, IB_PORT_LINK_WIDTH_ACTIVE_F);
	ispeed = mad_get_field(port->info, 0, IB_PORT_LINK_SPEED_ACTIVE_F);
	fdr10 = mad_get_field(port->ext_info, 0,
			      IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F) & FDR10;

	if (port->node->type == IB_NODE_SWITCH)
		info = (uint8_t *)&port->node->ports[0]->info;
	else
		info = (uint8_t *)&port->info;
	cap_mask = mad_get_field(info, 0, IB_PORT_CAPMASK_F);
	if (cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS))
		espeed = mad_get_field(port->info, 0,
				       IB_PORT_LINK_SPEED_EXT_ACTIVE_F);
	else
		espeed = 0;

	istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	iphysstate = mad_get_field(port->info, 0, IB_PORT_PHYS_STATE_F);

	switch (istate) {
		case IB_LINK_DOWN: totals.pn_down++; break;
		case IB_LINK_INIT: totals.pn_init++; break;
		case IB_LINK_ARMED: totals.pn_armed++; break;
		case IB_LINK_ACTIVE: totals.pn_active++; break;
		default:  totals.pn_undef++; break;
	}

	switch (iphysstate) {
		case IB_PORT_PHYS_STATE_DISABLED: totals.pn_disabled++; break;
		default: break;
	}

	if (istate == IB_LINK_ACTIVE) {
		switch (iwidth) {
			case IB_LINK_WIDTH_ACTIVE_1X: totals.pn_1x++; break;
			case IB_LINK_WIDTH_ACTIVE_4X: totals.pn_4x++; break;
			case IB_LINK_WIDTH_ACTIVE_8X: totals.pn_8x++; break;
			case IB_LINK_WIDTH_ACTIVE_12X: totals.pn_12x++; break;
			default:  totals.pn_undef++; break;
		}
		if (!espeed)
		{
			if (fdr10)
				totals.pn_fdr10++;
			else {
				switch (ispeed) {
					case IB_LINK_SPEED_ACTIVE_2_5:
						totals.pn_sdr++; break;
					case IB_LINK_SPEED_ACTIVE_5:
						totals.pn_ddr++; break;
					case IB_LINK_SPEED_ACTIVE_10:
						totals.pn_qdr++; break;
					default:  totals.pn_undef++; break;
				}
			}
		} else {
			switch (espeed) {
				case IB_LINK_SPEED_EXT_ACTIVE_14:
					totals.pn_fdr++; break;
				case IB_LINK_SPEED_EXT_ACTIVE_25:
					totals.pn_edr++; break;
				default:  totals.pn_undef++; break;
			}
		}
	}

	if (totals.pn_undef > n_undef) {
		printf("WARN: Undefined value found: ");
		cf_print_port(node_name, node, port, NULL, 1);
		check_node_rc = 1;
	}

	if (fabricconf)
		check_config(node_name, node, port);
	else
		check_basic_config(node_name, node, port);
}

static void check_addrs(ibnd_port_t *port)
{
	int checklid = 0;
	char *remap = NULL;

	if (!port)
		return;

	if (port->node->type == IB_NODE_SWITCH) {
		port = port->node->ports[0];
		if (!port)
			return;
	}

	remap = remap_node_name(node_name_map, port->node->guid,
				    port->node->nodedesc);

	checklid = mad_get_field(port->info, 0, IB_PORT_SMLID_F);

	if (smlid && smlid != checklid) {
		printf("ERROR smlid %d != %d (expected) on node %s\n", checklid,
				smlid, remap);
		check_node_rc = 1;
	}

	if (!port->base_lid) {
		printf("ERROR lid == 0 found on node %s\n", remap);
		check_node_rc = 1;
	}

	if (!port->guid) {
		printf("ERROR guid == 0 found on node %s\n", remap);
		check_node_rc = 1;
	}

	free(remap);
}

static void check_node(ibnd_node_t * node, void *user_data)
{
	int i = 0;
	char *remap =
	    remap_node_name(node_name_map, node->guid, node->nodedesc);

	for (i = 1; i <= node->numports; i++) {
		ibnd_port_t *port = node->ports[i];
		if (!port)
			continue;
		check_addrs(port);
		if (!port_seen(node->guid, i)) {
			check_port(remap, node, port);
			mark_port_seen(node->guid, i);
			if (port->remoteport) {
				mark_port_seen(port->remoteport->node->guid,
					port->remoteport->portnum);
				totals.num_ports++;
			}
		}
	}
	free(remap);
}

static int ignore_node(char *node_name, char *ignore_regex)
{
	static regex_t exp;
	static int regex_compiled = 0;
	static int regex_failed = 0;

	if (!ignore_regex)
		return 0;

	if (!regex_compiled) { /* only compile it one time */
		int rc;
		if ((rc = regcomp(&exp, ignore_regex, REG_ICASE |
				REG_NOSUB | REG_EXTENDED)) != 0) {
			fprintf(stderr, "ERROR: regcomp failed on \"%s\": %d\n",
				ignore_regex, rc);
			regex_failed = 1;
			return 0;
		}
		regex_compiled = 1;
	}

	return (regexec(&exp, node_name, 0, NULL, 0) == 0);
}

static void mark_node_seen(print_node_data_t *data, uint64_t node_guid)
{
	guid_list_item_t *i = malloc(sizeof(*i));
	if (i == NULL) {
		fprintf(stderr, "ERROR: malloc failed\n");
		return;
	}
	i->guid = node_guid;
	data->visited_nodes = g_slist_append(data->visited_nodes, (gpointer)i);
	data->vn_cnt++;
}

static int node_seen(print_node_data_t *data, uint64_t node_guid)
{
	int i = 0;
	for (i = 0; i < data->vn_cnt; i++) {
		gpointer t = g_slist_nth_data(data->visited_nodes, i);
		if (((guid_list_item_t *)t)->guid == node_guid)
			return (1);
	}
	return (0);
}

static void free_visited_nodes(print_node_data_t *data)
{
	if (!data->visited_nodes)
		return;

	g_slist_foreach(data->visited_nodes, free_node, NULL);
	g_slist_free(data->visited_nodes);
}

static void print_node_xml(ibnd_node_t *node, void *ud)
{
	print_node_data_t *data = (print_node_data_t *)ud;
	FILE *fp = data->fp;

	int header = 0;
	int i = 0;
	char * node_name = remap_node_name(node_name_map,
					node->guid,
					node->nodedesc);

	if (ignore_node(node_name, data->ignore_regex))
		goto ignore;

	for (i = 1; i <= node->numports; i++) {
		if (node->ports[i] && node->ports[i]->remoteport) {
			char *rem_name = remap_node_name(node_name_map,
					node->ports[i]->remoteport->node->guid,
					node->ports[i]->remoteport->node->nodedesc);
			if (!ignore_node(rem_name, data->ignore_regex)
			    && !node_seen(data,
					node->ports[i]->remoteport->node->guid)) {
				if (!header) {
					fprintf(fp, "\t<linklist name=\"%s\">\n", node_name);
					header = 1;
				}
				fprintf(fp, "\t\t<port num=\"%d\"", i);
				if (node->ports[i]->ext_portnum)
					fprintf(fp, " extnum=\"%d\"",
						node->ports[i]->ext_portnum);
				fprintf(fp, ">");

				fprintf(fp, "<r_port");
				if (node->ports[i]->remoteport->ext_portnum)
					fprintf(fp, " extnum=\"%d\"",
						node->ports[i]->remoteport->ext_portnum);
				fprintf(fp, ">%d</r_port>",
					node->ports[i]->remoteport->portnum);
				fprintf(fp, "<r_node>%s</r_node>", rem_name);
				fprintf(fp, "</port>\n");
			}
		} else if (data->print_missing) {
			if (!header) {
				fprintf(fp, "\t<linklist name=\"%s\">\n", node_name);
				header = 1;
			}
			fprintf(fp, "<!--\n");
			fprintf(fp, "\t\t<port num=\"%d\">", i);
			fprintf(fp, "<r_port>XXXXX</r_port>");
			fprintf(fp, "<r_node>YYYYY</r_node>");
			fprintf(fp, "</port>\n");
			fprintf(fp, "-->\n");
		}
	}
	if (header)
		fprintf(fp, "\t</linklist>\n");

	mark_node_seen(data, node->guid);

ignore:
	free(node_name);
}


int generate_from_fabric(ibnd_fabric_t *fabric, char *generate_file, nn_map_t *name_map,
			char *ignore_regex, int print_missing)
{
	print_node_data_t data;
	FILE *fp = NULL;
	node_name_map = name_map;

	if (strcmp(SCHEMA_VERSION, (char *)ibfc_schema_version()) != 0) {
		fprintf(stderr, "WARNING: generate schema version (%s) and "
				"libfabricconf schema version (%s) does not "
				"match\n", SCHEMA_VERSION, ibfc_schema_version);
	}

	memset(&data, 0, sizeof(data));
	fp = fopen(generate_file, "w+");
	if (!fp) {
		fprintf(stderr, "Failed to open %s: %s\n", generate_file, strerror(errno));
		return 1;
	}

	fprintf(fp, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
	fprintf(fp, "<ibfabric name=\"%s\" schemaVersion=\"%s\">\n",
		fabric_name, SCHEMA_VERSION);

	data.fp = fp;
	data.ignore_regex = ignore_regex;
	data.print_missing = print_missing;

	ibnd_iter_nodes_type(fabric, print_node_xml, IB_NODE_SWITCH, (void *)&data);
	ibnd_iter_nodes_type(fabric, print_node_xml, IB_NODE_CA, (void *)&data);
	ibnd_iter_nodes_type(fabric, print_node_xml, IB_NODE_ROUTER, (void *)&data);

	fprintf(fp, "</ibfabric>\n");

	free_visited_nodes(&data);

	fclose(fp);
	return 0;
}

int check_links(ib_portid_t *port_id,
		struct ibmad_port *ibmad_port,
		ibnd_fabric_t *fabric,
		nn_map_t *name_map,
		check_flags_t *flags)
{
	int rc = 0;
	node_name_map = name_map;
	ibnd_port_t *p = NULL;
	smlid = flags->sm_lid;
	print_addr_info = flags->print_addr_info;

	if (flags->downnodes_str)
		downnodes = nodelist_create(flags->downnodes_str);

	fabricconf = ibfc_alloc_conf();
	if (!fabricconf) {
		fprintf(stderr, "ERROR: Failed to alloc fabricconf\n");
		exit(1);
	}
	ibfc_set_warn_dup(fabricconf, 1);

	printf("Reading fabric conf file...\n");
	fflush(stdout);

	if (ibfc_parse_file(flags->fabricconffile, fabricconf)) {
		ibfc_free(fabricconf);
		fabricconf = NULL;
		check_node_rc = -1;
	}

	if (!fabricconf)
		printf("\nNo config file: Evaluating basic connectivity...\n");
	else
		printf("\nEvaluating connectivity...\n");

	if (!flags->all && flags->guid_str) {
		if ((p = ibnd_find_port_guid(fabric, flags->guid)) != NULL) {
			check_node(p->node, NULL);
			rc = check_node_rc;
		} else {
			fprintf(stderr, "Failed to find port: %s\n",
				flags->guid_str);
			rc = -1;
		}
	} else if (!flags->all && flags->dr_path) {
		uint64_t guid;
		uint8_t ni[IB_SMP_DATA_SIZE] = { 0 };

		if (!smp_query_via(ni, port_id, IB_ATTR_NODE_INFO, 0,
				   ibd_timeout, ibmad_port)){
			rc = -1;
			goto Exit;
		}
		mad_decode_field(ni, IB_NODE_GUID_F, &guid);

		if ((p = ibnd_find_port_guid(fabric, guid)) != NULL) {
			check_node(p->node, NULL);
			rc = check_node_rc;
		} else {
			fprintf(stderr, "Failed to find node: %s\n",
				flags->dr_path);
			rc = -1;
		}
	} else {
		ibnd_iter_nodes(fabric, check_node, NULL);
		rc = check_node_rc;
	}
	print_port_stats();

Exit:
	nodelist_destroy(downnodes);
	free_seen();
	return rc;
}

