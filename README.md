osmo-smlc - Osmocom Serving Mobile Location Centre
==================================================

This repository contains a C-language implementation of a minimalistic
GSM Serving Mobile Location Centre (SMLC) for 2G (GSM).  It is part of the
[Osmocom](https://osmocom.org/) Open Source Mobile Communications project.

OsmoSMLC exposes
 * 3GPP Lb interface towards the BSC
 * The Osmocom typical telnet VTY and CTRL interfaces.
 * The Osmocom typical statsd exporter.

OsmoSMLC supports the following location methods:
 * currently only the Timing Advance based method of determining a mobile station; operator must configure the
   locations of the cells in the osmo-smlc configuration file

Homepage
--------

You can find the OsmoSMLC issue tracker and wiki online at
<https://osmocom.org/projects/osmo-smlc> and <https://osmocom.org/projects/osmo-smlc/wiki>


GIT Repository
--------------

You can clone from the official osmo-smlc.git repository using

        git clone https://gitea.osmocom.org/cellular-infrastructure/osmo-smlc

There is a web interface at <https://gitea.osmocom.org/cellular-infrastructure/osmo-smlc>


Documentation
-------------

User Manuals and VTY reference manuals are [optionally] built in PDF form
as part of the build process.

Pre-rendered PDF version of the current "master" can be found at
[User Manual](https://ftp.osmocom.org/docs/latest/osmosmlc-usermanual.pdf)
as well as the [VTY Reference Manual](https://ftp.osmocom.org/docs/latest/osmosmlc-vty-reference.pdf)


Mailing List
------------

Discussions related to osmo-smlc are happening on the
openbsc@lists.osmocom.org mailing list, please see
<https://lists.osmocom.org/mailman/listinfo/openbsc> for subscription
options and the list archive.

Please observe the [Osmocom Mailing List
Rules](https://osmocom.org/projects/cellular-infrastructure/wiki/Mailing_List_Rules)
when posting.

Contributing
------------

Our coding standards are described at
<https://osmocom.org/projects/cellular-infrastructure/wiki/Coding_standards>

We use a Gerrit based patch submission/review process for managing
contributions.  Please see
<https://osmocom.org/projects/cellular-infrastructure/wiki/Gerrit> for
more details

The current patch queue for osmo-smlc can be seen at
<https://gerrit.osmocom.org/#/q/project:osmo-smlc+status:open>
