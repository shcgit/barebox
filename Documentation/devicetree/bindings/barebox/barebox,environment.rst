barebox environment
===================

This driver provides an environment for barebox from the devicetree.

Required properties:

* ``compatible``: should be ``barebox,environment``
* ``device-path``: path to the environment

The device-path is a multistring property. The first string should be a
nodepath to the node containing the physical device of the environment.
The subsequent strings are of the form <type>:<options> to further describe
the path to the environment. Supported values for <type>:

``partname``:<partname>
  This describes a partition on a device. <partname> can
  be the label for MTD partitions, the number for DOS
  partitions (beginning with 0) or the name for GPT partitions.

Example::

  environment@0 {
  	compatible = "barebox,environment";
  	device-path = &flash, "partname:barebox-environment";
  };
