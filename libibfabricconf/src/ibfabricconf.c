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

#include <stdio.h>
#include <errno.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <infiniband/ibfabricconf.h>
#include "ibfabricconf-int.h"

#ifndef LIBXML_TREE_ENABLED
#error "libxml error: Tree support not compiled in"
#endif


/** =========================================================================
 * Borrow from ibnetdiscover for debugging output
 */
char *dump_linkspeed_compat(uint8_t speed)
{
	switch (speed) {
	case IB_LINK_SPEED_ACTIVE_2_5:
		return ("2.5 Gbps");
		break;
	case IB_LINK_SPEED_ACTIVE_5:
		return ("5.0 Gbps");
		break;
	case IB_LINK_SPEED_ACTIVE_10:
		return ("10.0 Gbps");
		break;
	}
	return ("???");
}

char *dump_linkwidth_compat(uint8_t width)
{
	switch (width) {
	case IB_LINK_WIDTH_ACTIVE_1X:
		return ("1X");
		break;
	case IB_LINK_WIDTH_ACTIVE_4X:
		return ("4X");
		break;
	case IB_LINK_WIDTH_ACTIVE_8X:
		return ("8X");
		break;
	case IB_LINK_WIDTH_ACTIVE_12X:
		return ("12X");
		break;
	}
	return ("??");
}
/** =========================================================================
 * END borrow
 */

#define HTSZ 137
/* hash algo found here: http://www.cse.yorku.ca/~oz/hash.html */
static int
hash_name(char *str)
{
	unsigned long hash = 5381;
	int c;

	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return (hash % HTSZ);
}

struct ibfc_named_prop {
	struct ibfc_named_prop *next;
	char *name;
	char *val;
};

struct ibfc_prop {
	uint8_t speed;
	uint8_t width;
	struct ibfc_named_prop *named;
} IBCONF_DEFAULT_PROP =
{
	speed: IB_LINK_SPEED_ACTIVE_2_5,
	width: IB_LINK_WIDTH_ACTIVE_1X,
	named: NULL
};

struct ibfc_port {
	struct ibfc_port *next;
	struct ibfc_port *prev;
	char *name;
	int port_num;
	ibfc_prop_t prop;
	void *user_data;
	struct ibfc_port *remote;
};

struct ibfc_port_list {
	struct ibfc_port *head;
};

struct ibfc_conf {
	struct ibfc_port *ports[HTSZ];
	char *name;
	FILE *err_fd;
	int warn_dup;
};


static int
add_named_prop(ibfc_prop_t *prop, char *name, char *value)
{
	struct ibfc_named_prop *np = calloc(1, sizeof(struct ibfc_named_prop));
	if (!np)
		return (-ENOMEM);

	np->next = prop->named;
	prop->named = np;
	return (0);
}

static void
prop_free_named(ibfc_prop_t *p)
{
	struct ibfc_named_prop *np = p->named;
	struct ibfc_named_prop *tmp;
	while (np) {
		free(np->name);
		free(np->val);
		tmp = np;
		np = np->next;
		free(tmp);
	}
	p->named = NULL;
}

/**
 * duplicate properties.
 */
static int
dup_prop(ibfc_prop_t *dest, ibfc_prop_t *src)
{
	int rc = 0;
	struct ibfc_named_prop *np = src->named;

	dest->speed = src->speed;
	dest->width = src->width;

	prop_free_named(dest);
	while (np) {
		add_named_prop(dest, np->name, np->val);
		np = np->next;
	}
	return (rc);
}

static ibfc_port_t *
calloc_port(char *name, int port_num, ibfc_prop_t *prop)
{
	ibfc_port_t *port = calloc(1, sizeof *port);
	if (!port)
		return (NULL);

	port->next = NULL;
	port->prev = NULL;
	port->name = strdup(name);
	port->port_num = port_num;
	dup_prop(&port->prop, prop);
	return (port);
}

static inline void
free_port(ibfc_port_t *p)
{
	free(p->name);
	prop_free_named(&p->prop);
	free(p);
}

static int
port_equal(ibfc_port_t *port, char *n, int p)
{
	return (strcmp((const char *)port->name, (const char *)n) == 0 && port->port_num == p);
}

static ibfc_port_t *
find_port(ibfc_conf_t *fabricconf, char *n, int p)
{
	ibfc_port_t *cur;
	int h = hash_name(n);

	for (cur = fabricconf->ports[h]; cur; cur = cur->next)
		if (port_equal(cur, n, p))
			return (cur);

	return (NULL);
}

static int
remove_free_port(ibfc_conf_t *fabricconf, ibfc_port_t *port)
{
	if (port->prev)
		port->prev->next = port->next;
	else {
		int h = hash_name(port->name);
		fabricconf->ports[h] = port->next;
	}

	if (port->next)
		port->next->prev = port->prev;

	free_port(port);
	return (0);
}

static int
add_port(ibfc_conf_t *fabricconf, ibfc_port_t *port)
{
	ibfc_port_t *prev = NULL;
	ibfc_port_t *last = NULL;
	int h = hash_name(port->name);
	port->next = fabricconf->ports[h];
	port->prev = NULL;
	if (fabricconf->ports[h])
	{
		last = fabricconf->ports[h];
		prev = fabricconf->ports[h];
		while (last) {
			prev = last;
			last = last->next;
		}
		port->prev = prev;
		prev->next = port;
		port->next = NULL;
	} else
		fabricconf->ports[h] = port;
	return (0);
}

static ibfc_port_t *
calloc_add_port(ibfc_conf_t *fabricconf, char *name, int port_num,
	ibfc_prop_t *prop)
{
	ibfc_port_t *port = calloc_port(name, port_num, prop);
	if (!port)
		return (NULL);
	add_port(fabricconf, port);
	return (port);
}

static int
add_link(ibfc_conf_t *fabricconf, char *lname, char *lport_str,
	ibfc_prop_t *prop, char *rname, char *rport_str)
{
	int found = 0;
	int lport_alloc = 0;
	ibfc_port_t *lport, *rport;
	unsigned long lpn, rpn;

	lpn = strtoul(lport_str, NULL, 0);
	rpn = strtoul(rport_str, NULL, 0);

	/* No need to check errno
	 * both 0 and ULONG_MAX are invalid port numbers
	 */
	if (lpn == 0 || lpn == ULONG_MAX ||
	    rpn == 0 || rpn == ULONG_MAX) {
		fprintf(fabricconf->err_fd,
			"ERROR: Invalid port number (\"%s\" or \"%s\") for "
			"link \"%s\":%s  ---> \"%s\":%s\n",
			lport_str, rport_str,
			lname, lport_str, rname, rport_str);
		return (-EINVAL);
	}

	lport = find_port(fabricconf, lname, lpn);
	rport = find_port(fabricconf, rname, rpn);

	if (lport) {
		assert(lport->remote->remote == lport);
		if (fabricconf->warn_dup) {
			fprintf(fabricconf->err_fd,
				"WARN: redefining port "
				"\"%s\":%d  ---> %d:\"%s\"\n",
				lport->name, lport->port_num,
				lport->remote->port_num, lport->remote->name);
			found = 1;
		}
		if (lport->remote != rport)
			remove_free_port(fabricconf, lport->remote);
		dup_prop(&lport->prop, prop);
	} else {
		lport = calloc_add_port(fabricconf, lname, lpn, prop);
		if (!lport) {
			fprintf(fabricconf->err_fd, "ERROR: failed to allocated lport\n");
			return (-ENOMEM);
		}
		lport_alloc = 1;
	}

	if (rport) {
		assert(rport->remote->remote == rport);
		if (fabricconf->warn_dup) {
			fprintf(fabricconf->err_fd, "WARN: redefining port "
				"\"%s\":%d  ---> %d:\"%s\"\n",
				rport->name, rport->port_num,
				rport->remote->port_num, rport->remote->name);
			found = 1;
		}
		if (rport->remote != lport)
			remove_free_port(fabricconf, rport->remote);
		dup_prop(&rport->prop, prop);
	} else {
		rport = calloc_add_port(fabricconf, rname, rpn, prop);
		if (!rport) {
			fprintf(fabricconf->err_fd, "ERROR: failed to allocated lport\n");
			if (lport_alloc)
				free(lport);
			return (-ENOMEM);
		}
	}

	if (found) {
		fprintf(fabricconf->err_fd, "      New Link: \"%s\":%d  <-->  %d:\"%s\"\n",
			lport->name, lport->port_num,
			rport->port_num, rport->name);
	}

	lport->remote = rport;
	rport->remote = lport;

	return (0);
}

/**
 * Search for and set the properties see in this node
 */
static int
parse_properties(xmlNode *node, ibfc_prop_t *prop)
{
	int rc = 0;
	xmlAttrPtr attr = NULL;

	for(attr = node->properties; NULL != attr; attr = attr->next)
	{
		char *name = (char *)attr->name;
		char *value = (char *)attr->children->content;

		/* skip the properties we know about */
		if (strcmp(name, "num") == 0)
			continue;
		if (strcmp(name, "name") == 0)
			continue;
		if (strcmp(name, "model") == 0)
			continue;
		if (strcmp(name, "position") == 0)
			continue;

		/* process the "special" properties */
		if (strcmp(name, "speed") == 0) {
			if (strcmp(value, "SDR") == 0)
				prop->speed = IB_LINK_SPEED_ACTIVE_2_5;
			if (strcmp(value, "DDR") == 0)
				prop->speed = IB_LINK_SPEED_ACTIVE_5;
			if (strcmp(value, "QDR") == 0)
				prop->speed = IB_LINK_SPEED_ACTIVE_10;

			continue;
		}

		if (strcmp(name, "width") == 0) {
			if (strcmp(value, "1x") == 0 ||
			    strcmp(value, "1X") == 0)
				prop->width = IB_LINK_WIDTH_ACTIVE_1X;
			if (strcmp(value, "4x") == 0 ||
			    strcmp(value, "4X") == 0)
				prop->width = IB_LINK_WIDTH_ACTIVE_4X;
			if (strcmp(value, "8x") == 0 ||
			    strcmp(value, "8X") == 0)
				prop->width = IB_LINK_WIDTH_ACTIVE_8X;
			if (strcmp(value, "12x") == 0 ||
			    strcmp(value, "12X") == 0)
				prop->width = IB_LINK_WIDTH_ACTIVE_12X;

			continue;
		}

		/* process user defined */
		rc = add_named_prop(prop, name, value);
		if (rc)
			break;
	}

	return (rc);
}

/** =========================================================================
 * Properties sitting on the stack may have memory allocated when they are
 * parsed into:
 * free this memory if necessary
 */
static inline void
free_stack_prop(ibfc_prop_t *p)
{
	prop_free_named(p);
}

typedef struct ch_pos_map {
	struct ch_pos_map *next;
	char *pos;
	char *name;
} ch_pos_map_t;

typedef struct ch_map {
	char *name;
	ch_pos_map_t *map;
} ch_map_t;

static char *
map_pos(ch_map_t *ch_map, char *position)
{
	ch_pos_map_t *cur;
	for (cur = ch_map->map; cur; cur = cur->next) {
		if (strcmp(position, cur->pos) == 0)
			return (cur->name);
	}
	return (NULL);
}

static int
remap_linklist(xmlNode *linklist, ch_map_t *ch_map, ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur;
	for (cur = linklist->children; cur; cur = cur->next) {
		if (strcmp((char *)cur->name, "port") == 0) {
			xmlNode *child;
			for (child = cur->children; child; child = child->next) {
				if (strcmp((char *)child->name, "r_node") == 0) {
					char *pos = (char *)xmlNodeGetContent(child);
					char *name = NULL;

					if (!pos) {
						fprintf(fabricconf->err_fd,
							"ERROR: position not specified in "
							"r_node\n");
						rc = -EIO;
						goto exit;
					}

					name = map_pos(ch_map, pos);
					if (name)
						xmlNodeSetContent(child, (xmlChar *)name);
					else {
						char n[256];
						snprintf(n, 256, "%s %s",
							ch_map->name, pos);
						xmlNodeSetContent(child, (xmlChar *)n);
					}
					xmlFree(pos);
				}
			}
		}
	}
exit:
	return (rc);
}

static int
remap_chassis_doc(xmlNode *chassis, ch_map_t *ch_map, ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur;
	for (cur = chassis->children; cur; cur = cur->next) {
		if (strcmp((char *)cur->name, "linklist") == 0) {
			char *pos = (char *)xmlGetProp(cur, (xmlChar *)"position");
			char *name = NULL;

			if (!pos) {
				fprintf(fabricconf->err_fd,
					"ERROR: position not specified in "
					"linklist\n");
				rc = -EIO;
				goto exit;
			}

			name = map_pos(ch_map, pos);
			if (name)
				xmlSetProp(cur, (xmlChar *)"name", (xmlChar *)name);
			else {
				char n[256];
				snprintf(n, 256, "%s %s", ch_map->name, pos);
				xmlSetProp(cur, (xmlChar *)"name", (xmlChar *)n);
			}
			xmlFree(pos);

			rc = remap_linklist(cur, ch_map, fabricconf);
			if (rc)
				goto exit;
		}
	}
exit:
	return (rc);
}

static int
parse_port(char *node_name, xmlNode *portNode, ibfc_prop_t *parent_prop,
		ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur = NULL;
	char *port = (char *)xmlGetProp(portNode, (xmlChar *)"num");
	ibfc_prop_t prop = IBCONF_DEFAULT_PROP;
	char *r_port = NULL;
	char *r_node = NULL;

	if (!port)
		return (-EIO);

	dup_prop(&prop, parent_prop); /* inherit the properties from our parent */
	parse_properties(portNode, &prop); /* fill in with anything new */

	for (cur = portNode->children;
	     cur;
	     cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "r_port") == 0) {
				r_port = (char *)xmlNodeGetContent(cur);
			}
			if (strcmp((char *)cur->name, "r_node") == 0) {
				r_node = (char *)xmlNodeGetContent(cur);
			}
		}
	}

	rc = add_link(fabricconf, (char *)node_name, (char *)port, &prop,
		(char *)r_node, (char *)r_port);

	xmlFree(port);
	xmlFree(r_port);
	xmlFree(r_node);
	free_stack_prop(&prop);

	return (rc);
}

static int
parse_linklist(xmlNode *linklist, ibfc_prop_t *parent_prop,
		ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur = NULL;
	char *linklist_name = (char *)xmlGetProp(linklist, (xmlChar *)"name");
	ibfc_prop_t prop = IBCONF_DEFAULT_PROP;

	if (!linklist_name)
		return (-EIO);

	dup_prop(&prop, parent_prop); /* inherit the properties from our parent */
	parse_properties(linklist, &prop); /* fill in with anything new */

	for (cur = linklist->children;
	     cur;
	     cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "port") == 0) {
				rc = parse_port(linklist_name, cur, &prop, fabricconf);
			}
		}
		if (rc)
			break;
	}
	free_stack_prop(&prop);
	xmlFree(linklist_name);
	return (rc);
}

static int parse_chassismap(xmlNode *chassis, ibfc_prop_t *parent_prop,
				ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur;
	for (cur = chassis->children; cur; cur = cur->next) {
		if (strcmp((char *)cur->name, "linklist") == 0) {
			rc = parse_linklist(cur, parent_prop, fabricconf);
		}
		if (rc)
			break;
	}
	return (rc);
}

static int
process_chassis_model(ch_map_t *ch_map, char *model,
			ibfc_prop_t *parent_prop, ibfc_conf_t *fabricconf)
{
	xmlDoc *chassis_doc = NULL;
	xmlNode *root_element = NULL;
	xmlNode *cur = NULL;
	ibfc_prop_t prop = *parent_prop;
	int rc = 0;
	char *file = malloc(strlen(IBFC_CHASSIS_CONF_DIR) + strlen(model)
				+strlen("/.xml   "));

	snprintf(file, 512, IBFC_CHASSIS_CONF_DIR"/%s.xml", model);

	/*parse the file and get the DOM */
	chassis_doc = xmlReadFile(file, NULL, 0);

	if (chassis_doc == NULL) {
		fprintf(fabricconf->err_fd, "ERROR: could not parse chassis file %s\n", file);
		rc = -EIO;
		goto exit;
	}

	/*Get the root element node */
	root_element = xmlDocGetRootElement(chassis_doc);

	/* replace all the content of this tree with our ch_pos_map */
	for (cur = root_element; cur; cur = cur->next)
		if (cur->type == XML_ELEMENT_NODE)
			if (strcmp((char *)cur->name, "chassismap") == 0) {
				char *model_name = (char *)xmlGetProp(cur, (xmlChar *)"model");
				if (!model_name || strcmp(model_name, model) != 0) {
					fprintf(fabricconf->err_fd, "ERROR processing %s; Model name does not "
						"match: %s != %s\n",
						file, model_name, model);
					rc = -EIO;
					goto exit;
				}
				xmlFree(model_name);

				rc = remap_chassis_doc(cur, ch_map, fabricconf);
				if (rc) {
					fprintf(fabricconf->err_fd,
						"ERROR: could not parse chassis file %s\n", file);
					goto free_doc_exit;
				}
				parse_chassismap(cur, &prop, fabricconf);
			}

free_doc_exit:
	xmlFreeDoc(chassis_doc);
exit:
	free(file);
	return (rc);
}

/**
 * Parse chassis
 */
static int
parse_chassis(xmlNode *chassis, ibfc_prop_t *parent_prop,
		ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur = NULL;
	ibfc_prop_t prop = IBCONF_DEFAULT_PROP;
	xmlChar *chassis_name = xmlGetProp(chassis, (xmlChar *)"name");
	xmlChar *chassis_model = xmlGetProp(chassis, (xmlChar *)"model");
	ch_map_t *ch_map = calloc(1, sizeof *ch_map);

	if (!ch_map) {
		rc = -ENOMEM;
		goto free_xmlChar;
	}

	if (!chassis_name || !chassis_model) {
		fprintf(fabricconf->err_fd, "chassis_[name|model] not defined\n");
		rc = -EIO;
		goto free_xmlChar;
	}

	ch_map->name = (char *)chassis_name;

	dup_prop(&prop, parent_prop); /* inherit the properties from our parent */
	parse_properties(chassis, &prop); /* fill in with anything new */

	/* first get a position/name map */
	for (cur = chassis->children;
	     cur;
	     cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "node") == 0) {
				ch_pos_map_t *n = NULL;
				char *pos = (char *)xmlGetProp(cur, (xmlChar *)"position");
				char *name = (char *)xmlNodeGetContent(cur);

				if (!pos || !name)
				{
					fprintf(fabricconf->err_fd, "Error "
						"processing chassis \"%s\": "
						"node \"%s\" position \"%s\""
						"\n",
						ch_map->name,
						name ? name : "<unknown>",
						pos ? pos : "<unknown>");
						rc = -EIO;
						goto free_pos_name_map;
				}

				n = malloc(sizeof *n);
				n->pos = pos;
				n->name = name;
				n->next = ch_map->map;
				ch_map->map = n;
			}
		}
	}

	/* then use that map to create real links with those names */
	/* read the model config */
	rc = process_chassis_model(ch_map, (char *)chassis_model, &prop, fabricconf);

free_pos_name_map:
	/* free our position/name map */
	while (ch_map->map) {
		ch_pos_map_t *tmp = ch_map->map;
		ch_map->map = ch_map->map->next;
		xmlFree(tmp->pos);
		xmlFree(tmp->name);
		free(tmp);
	}

	free_stack_prop(&prop);
	free(ch_map);

free_xmlChar:
	/* frees the memory pointed to in ch_map_t as well */
	xmlFree(chassis_name);
	xmlFree(chassis_model);

	return (rc);
}


static int
parse_fabric(xmlNode *fabric, ibfc_prop_t *parent_prop,
		ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur = NULL;
	xmlAttr *attr = NULL;
	ibfc_prop_t prop = IBCONF_DEFAULT_PROP;
	xmlChar *fabric_name = xmlGetProp(fabric, (xmlChar *)"name");

	if (fabric_name) {
		fabricconf->name = strdup((char *)fabric_name);
		xmlFree(fabric_name);
	} else
		fabricconf->name = strdup("fabric");

	dup_prop(&prop, parent_prop); /* inherit the properties from our parent */
	parse_properties(fabric, &prop); /* fill in with anything new */

	for (cur = fabric->children; cur; cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "chassis") == 0)
				rc = parse_chassis(cur, &prop, fabricconf);
			else if (strcmp((char *)cur->name, "linklist") == 0)
				rc = parse_linklist(cur, &prop, fabricconf);
			else if (strcmp((char *)cur->name, "subfabric") == 0)
				rc = parse_fabric(cur, &prop, fabricconf);
			else {
				xmlChar * cont = xmlNodeGetContent(cur);
				fprintf(fabricconf->err_fd, "UNKNOWN XML node found\n");
				fprintf(fabricconf->err_fd, "%s = %s\n", cur->name, (char *)cont);
				xmlFree(cont);
				/* xmlGetProp(node, "key") could work as well */
				for (attr = cur->properties; attr; attr = attr->next) {
					fprintf(fabricconf->err_fd, "   %s=%s\n",
					(char *)attr->name, (char *)attr->children->content);
				}
			}
		}
		if (rc)
			break;
	}
	free_stack_prop(&prop);
	return (rc);
}

/**
 */
static int
parse_file(xmlNode * a_node, ibfc_conf_t *fabricconf)
{
	xmlNode *cur = NULL;
	ibfc_prop_t prop = IBCONF_DEFAULT_PROP;

	for (cur = a_node; cur; cur = cur->next)
		if (cur->type == XML_ELEMENT_NODE)
			if (strcmp((char *)cur->name, "fabric") == 0)
				return (parse_fabric(cur, &prop, fabricconf));

	return (-EIO);
}



/** =========================================================================
 * External interface functions
 */

/* accessor function */
char *ibfc_conf_get_name(ibfc_conf_t *conf) { return (conf->name); }
int ibfc_prop_get_speed(ibfc_prop_t *prop) { return (prop->speed); }
int ibfc_prop_get_width(ibfc_prop_t *prop) { return (prop->width); }
char *ibfc_port_get_name(ibfc_port_t *port)
{
	return (port->name);
}
int   ibfc_port_get_port_num(ibfc_port_t *port) { return (port->port_num); }
ibfc_prop_t *ibfc_port_get_prop(ibfc_port_t *port)
	{ return (&port->prop); }
ibfc_port_t *ibfc_port_get_remote(ibfc_port_t *port)
	{ return (port->remote); }
void  ibfc_port_set_user(ibfc_port_t *port, void *user_data)
	{ port->user_data = user_data; }
void *ibfc_port_get_user(ibfc_port_t *port) { return (port->user_data); }

char *
ibfc_sprintf_prop(char ret[], unsigned n, ibfc_port_t *port)
{
	if (!n)
		return (NULL);

	ret[0] = '\0';
	snprintf(ret, n, "%s %s",
		dump_linkwidth_compat(port->prop.width),
		dump_linkspeed_compat(port->prop.speed));
	return (ret);
}


/* interface functions */
ibfc_conf_t *
ibfc_alloc_conf(void)
{
	ibfc_conf_t *rc = calloc(1, sizeof *rc);
	if (!rc)
		return (NULL);

	rc->err_fd = stderr;
	rc->warn_dup = 0;
	return (rc);
}

static void
ibfc_free_ports(ibfc_conf_t *fabricconf)
{
	int i = 0;
	for (i = 0; i < HTSZ; i++) {
		ibfc_port_t *port = fabricconf->ports[i];
		while (port) {
			ibfc_port_t *tmp = port;
			port = port->next;
			free_port(tmp);
		}
		fabricconf->ports[i] = NULL;
	}
}

void
ibfc_free(ibfc_conf_t *fabricconf)
{
	if (!fabricconf)
		return;
	ibfc_free_ports(fabricconf);
	free(fabricconf);
}

void ibfc_set_stderr(ibfc_conf_t *fabricconf, FILE *f)
{
	if (fabricconf)
		fabricconf->err_fd = f;
}
void ibfc_set_warn_dup(ibfc_conf_t *fabricconf, int warn_dup)
{
	if (fabricconf)
		fabricconf->warn_dup = warn_dup;
}

int
ibfc_parse_file(char *file, ibfc_conf_t *fabricconf)
{
	xmlDoc *doc = NULL;
	xmlNode *root_element = NULL;
	int rc = 0;

	if (!fabricconf)
		return (-EINVAL);

	ibfc_free_ports(fabricconf);

	/* initialize the library */
	LIBXML_TEST_VERSION

	if (!file) {
		file = IBFC_DEF_CONFIG;
	}

	/* parse the file and get the DOM */
	doc = xmlReadFile(file, NULL, 0);
	if (doc == NULL) {
		fprintf(fabricconf->err_fd, "error: could not parse file %s\n", file);
		return (-EIO);
	}

	/*Get the root element node */
	root_element = xmlDocGetRootElement(doc);

	/* process the file */
	rc = parse_file(root_element, fabricconf);

	/*free the document */
	xmlFreeDoc(doc);

	/*
	 *Free the global variables that may
	 *have been allocated by the parser.
	 */
	xmlCleanupParser();

	return (rc);
}

ibfc_port_t *
ibfc_get_port(ibfc_conf_t *fabricconf, char *name, int p_num)
{
	return (find_port(fabricconf, name, p_num));
}

void
ibfc_free_port_list(ibfc_port_list_t *port_list)
{
	ibfc_port_t *head = port_list->head;
	while (head) {
		ibfc_port_t *tmp = head;
		head = head->next;
		free_port(tmp);
	}
	free(port_list);
}

int
ibfc_get_port_list(ibfc_conf_t *fabricconf, char *name,
		ibfc_port_list_t **list)
{
	ibfc_port_list_t *port_list = NULL;
	ibfc_port_t *cur = NULL;
	*list = NULL;
	int h = hash_name(name);

	port_list = calloc(1, sizeof *port_list);
	if (!port_list)
		return (-ENOMEM);

	for (cur = fabricconf->ports[h]; cur; cur = cur->next)
		if (strcmp((const char *)cur->name, (const char *)name) == 0) {
			ibfc_port_t *tmp = NULL;
			tmp = calloc_port(cur->name, cur->port_num, &cur->prop);
			if (!tmp) {
				ibfc_free_port_list(port_list);
				return (-ENOMEM);
			}
			tmp->remote = cur->remote;
			tmp->next = port_list->head;
			port_list->head = tmp;
		}

	*list = port_list;
	return (0);
}

void
ibfc_iter_ports(ibfc_conf_t *fabricconf, process_port_func func,
		void *user_data)
{
	int i = 0;
	for (i = 0; i < HTSZ; i++) {
		ibfc_port_t *port = NULL;
		for (port = fabricconf->ports[i]; port; port = port->next)
			func(port, user_data);
	}
}

void
ibfc_iter_port_list(ibfc_port_list_t *port_list,
			process_port_func func, void *user_data)
{
	ibfc_port_t *port = NULL;
	for (port = port_list->head; port; port = port->next)
		func(port, user_data);
}

char *
ibfc_prop_get(ibfc_prop_t *prop, char *prop_name)
{
	struct ibfc_named_prop *np = prop->named;

	if (strcmp(prop_name, "speed") == 0) {
		return (dump_linkspeed_compat(prop->speed));
	}
	if (strcmp(prop_name, "width") == 0) {
		return (dump_linkwidth_compat(prop->width));
	}
	while (np) {
		if (strcmp(np->name, prop_name) == 0)
			return (np->val);
		np = np->next;
	}
	return ("");
}

