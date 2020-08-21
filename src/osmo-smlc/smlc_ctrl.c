#include <osmocom/ctrl/control_if.h>


/*! \brief control interface lookup function for bsc/bts/msc gsm_data
 * \param[in] data Private data passed to controlif_setup()
 * \param[in] vline Vector of the line holding the command string
 * \param[out] node_type type (CTRL_NODE_) that was determined
 * \param[out] node_data private dta of node that was determined
 * \param i Current index into vline, up to which it is parsed
 */
int smlc_ctrl_node_lookup(void *data, vector vline, int *node_type,
			  void **node_data, int *i)
{
	return 0;
}


