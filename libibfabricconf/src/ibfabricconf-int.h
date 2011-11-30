
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

#ifndef _IBFABRICCONF_INT_H_
#define _IBFABRICCONF_INT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* These are temporary to get rid of ib_types.h dependancy */
#define IB_LINK_WIDTH_ACTIVE_1X			1
#define IB_LINK_WIDTH_ACTIVE_4X			2
#define IB_LINK_WIDTH_ACTIVE_8X			4
#define IB_LINK_WIDTH_ACTIVE_12X 		8
#define IB_LINK_SPEED_ACTIVE_2_5		1
#define IB_LINK_SPEED_ACTIVE_5			2
#define IB_LINK_SPEED_ACTIVE_10			4

#ifdef __cplusplus
}
#endif

#endif /* _IBFABRICCONF_INT_H_ */

