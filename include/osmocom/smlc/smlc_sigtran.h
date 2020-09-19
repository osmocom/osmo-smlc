#pragma once

int smlc_sigtran_init(void);
int smlc_sigtran_send(uint32_t sccp_conn_id, struct msgb *msg);
int smlc_sigtran_send_udt(uint32_t sccp_conn_id, struct msgb *msg);
