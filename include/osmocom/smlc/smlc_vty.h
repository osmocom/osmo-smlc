#pragma once

#include <osmocom/vty/command.h>

enum smlc_vty_node {
	CELLS_NODE = _LAST_OSMOVTY_NODE + 1,
};

void smlc_vty_init(struct vty_app_info *vty_app_info);
