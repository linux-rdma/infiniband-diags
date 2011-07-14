.. Common text for the ibfabricconf file

ibfabricconf.xml generation
---------------------------

Although not the intended use case is it understood that the generation of this
config file from an existing fabric can be very useful.  The following options
allow for this.

**--generate-config <config>**
Generate a config file from a scan of the fabric.  NOTE: this config file
is generic in nature as it does not include any link parameters such
as speed or width.  However, by default the speed and width is check against
the maximum supported by both ends of the link as reported by the hardware.
Should the user wish to support links operating below the maximum they can
alter this file as appropriate.

**--ignore <regex>** when generating a config skip nodes matching <regex>

**--missing** insert place holders for disconnected ports



