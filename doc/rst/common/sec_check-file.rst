.. Common text for the ibfabricconf file

IB FABRIC CONFIG FILE
---------------------

NOTE: IB FABRIC CONFIG FILE support is only available when XML support was
enabled at build time.


@IBDIAG_CONFIG_PATH@/ibfabricconf.xml

This global config file is provided to describe the IB fabric to some of the
diagnostics for verification of the fabric.  This file provides a list of link
definitions which provide a "logical" view of the fabric via node names and
ports.  This config file also provides for the configuration of link
speed/width for sets of "links".

This config file has the following features:

   * A logical view of the fabric is described by this file. (ie entry's are
     "name" based)

	      There are no GUID's, LIDs, etc.  As such misnamed nodes (possibly
	      supplied by the config file "ib-node-name-map" or by
	      miss-programmed node description fields) will show up as errors.
	      The reason for this is that this config file is designed to be used by
	      system admins to debug problems in a known network layout.  GUID's can
	      change as bad hardware is swapped out.  Therefore a logical view
	      of the fabric is much more useful and presented in
	      ibfabricconf.xml

   * Ports which are not specified are assumed to be down.

              Checks during a fabric scan will report active links on those ports as errors.

   * Properties are inherited from parent to child in the XML.

	      For example if the entire fabric is to be 4X/QDR then only the
	      fabric XML tag needs to be specified as 'speed="QDR" width="4X"'.
	      However, any link/node specification can  have  properties  set
	      which overrides itself and it's children.

   * Ports which are listed a second time will over ride the previous entry.

	      This is very useful for overriding the internal links of the
	      large switches after using the "chassis" tag.  On the other hand
	      caution should be used in generating your config files.

   * Common "chassis" configs can be included using the "chassis" tag.

	This makes defining the internal connections of large switches very
	easy.  Using a chassis tag will automatically create all the internal
	links for those switches.  The name of the chassis will be prepended to
	the internal switches found in the chassis.  For example::

              <chassis name="ibcore1" model="QLogic_9240"></chassis>

	results to the following names::

              "ibcore1 SP1a", "ibcore1 SP1b", etc.

              "ibcore1 L1a", "ibcore1 L2a", "ibcore1 Leaf 3 Chip A", etc.

	Internal switch names can be changed if the names used in the config file are not sufficient.

	      (See @IBDIAG_CONFIG_PATH@/chassis_fabricconfs for a list of
	      chassis which have alredy been defined.  Submissions of additional
	      chasis config files are always welcome.)

   * A "subfabric" tag allows you to specify the properties for a group of entries.


Example file::

	<?xml version="1.0" encoding="ISO-8859-1"?>
	<!DOCTYPE ibfabric>

	<ibfabric name="test fabric" schemaVersion="1.0">
		<property name="type">QSFP</property>
		<chassis name="test chassis" model="QLogic_9240"></chassis>
		<linklist name="SDR switch" speed="SDR">
			<port num="1"><r_port>1</r_port><r_node>SDR node</r_node></port>
		</linklist>
		<linklist name="DDR switch" speed="DDR">
			<port num="1"><r_port>1</r_port><r_node>DDR node</r_node></port>
		</linklist>
		<linklist name="QDR switch" speed="QDR">
			<port num="1"><r_port>1</r_port><r_node>QDR node</r_node></port>
		</linklist>
		<linklist name="FDR switch" speed="FDR">
			<port num="1"><r_port>1</r_port><r_node>FDR node</r_node></port>
		</linklist>
		<linklist name="FDR10 switch" speed="FDR10">
			<port num="1"><r_port>1</r_port><r_node>FDR10 node</r_node></port>
		</linklist>
		<linklist name="EDR switch" speed="EDR">
			<port num="1"><r_port>1</r_port><r_node>EDR node</r_node></port>
		</linklist>
		<linklist name="1x switch" width="1x">
			<port num="1"><r_port>1</r_port><r_node>1x node</r_node></port>
		</linklist>
		<linklist name="4x switch" width="4x">
			<port num="1"><r_port>1</r_port><r_node>4x node</r_node></port>
		</linklist>
		<linklist name="8x switch" width="8x">
			<port num="1"><r_port>1</r_port><r_node>8x node</r_node></port>
		</linklist>
		<linklist name="12x switch" width="12x">
			<port num="1"><r_port>1</r_port><r_node>12x node</r_node></port>
		</linklist>
		<subfabric width="1x" speed="SDR">
			<property name="color">blue</property>
			<!-- blue sub fabric -->
			<linklist name="1X SDR blue switch">
				<port num="1"><r_port>2</r_port><r_node>SDR blue node</r_node></port>
			</linklist>
		</subfabric>
	</ibfabric>

