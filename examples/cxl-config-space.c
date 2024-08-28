// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * This file is part of libcxlmi.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <libcxlmi.h>

unsigned int debug_mode = 0;

static int show_memdev_info(struct cxlmi_endpoint *ep)
{
	int rc;
	struct cxlmi_cmd_memdev_identify id;

	rc = cxlmi_cmd_memdev_identify(ep, NULL, &id);
	if (rc)
		return rc;

	printf("FW revision: %s\n", id.fw_revision);
	printf("total capacity: %ld Mb\n", 256 * id.total_capacity);
	printf("\tvolatile: %ld Mb\n", 256 * id.volatile_capacity);
	printf("\tpersistent: %ld Mb\n", 256 * id.persistent_capacity);
	printf("lsa size: %d bytes\n", id.lsa_size);
	printf("poison injection limit: %d\n", id.inject_poison_limit);
	printf("poison caps 0x%x\n", id.poison_caps);
	printf("DC event log size %d\n", id.dc_event_log_size);

       return 0;
}

static int show_some_info_from_all_devices(struct cxlmi_ctx *ctx)
{
	int rc = 0;
	struct cxlmi_endpoint *ep;

	cxlmi_for_each_endpoint(ctx, ep) {
		struct cxlmi_cmd_identify id;

		rc = cxlmi_cmd_identify(ep, NULL, &id);
		if (rc)
			break;

		printf("serial number: 0x%lx\n", (uint64_t)id.serial_num);

		switch (id.component_type) {
		case 0x00:
			printf("device type: CXL Switch\n");
			printf("VID:%04x DID:%04x\n", id.vendor_id, id.device_id);
			break;
		case 0x03:
			printf("device type: CXL Type3 Device\n");
			printf("VID:%04x DID:%04x SubsysVID:%04x SubsysID:%04x\n",
			       id.vendor_id, id.device_id,
			       id.subsys_vendor_id, id.subsys_id);

			show_memdev_info(ep);
			break;
		case 0x04:
			printf("GFD not supported\n");
			/* fallthrough */
		default:
			break;
		}
	}

	return rc;
}

static int toggle_abort(struct cxlmi_endpoint *ep)
{
	int rc;
	struct cxlmi_cmd_bg_op_status sts;

	rc = cxlmi_cmd_bg_op_status(ep, NULL, &sts);
	if (rc)
		goto done;

	if (!(sts.status & (1 << 0))) {
		printf("no background operation in progress...\n");

		rc = cxlmi_cmd_memdev_sanitize(ep, NULL);
		if (rc && rc != CXLMI_RET_BACKGROUND) {
			printf("could not start sanitize: %s\n",
			       cxlmi_cmd_retcode_tostr(rc));
			goto done;
		} else {
			printf("sanitizing op started\n");
			sleep(1);
		}
	}

	rc = cxlmi_cmd_request_bg_op_abort(ep, NULL);
	if (rc) {
		if (rc > 0)
			printf("request_bg_operation_abort error: %s\n",
			       cxlmi_cmd_retcode_tostr(rc));
	} else
		printf("background operation abort requested\n");
done:
	return rc;
}

static int play_with_device_timestamp(struct cxlmi_endpoint *ep)
{
	int rc;
	uint64_t orig_ts;
	struct cxlmi_cmd_get_timestamp get_ts;
	struct cxlmi_cmd_set_timestamp set_ts = {
		.timestamp = 946684800, /* Jan 1, 2000 */
	};

	rc = cxlmi_cmd_get_timestamp(ep, NULL, &get_ts);
	if (rc)
		return rc;
	printf("device timestamp: %lu\n", get_ts.timestamp);
	orig_ts = get_ts.timestamp;

	rc = cxlmi_cmd_set_timestamp(ep, NULL, &set_ts);
	if (rc)
		return rc;

	memset(&get_ts, 0, sizeof(get_ts));
	rc = cxlmi_cmd_get_timestamp(ep, NULL, &get_ts);
	if (rc)
		return rc;
	printf("new device timestamp: %lu\n", get_ts.timestamp);

	memset(&set_ts, 0, sizeof(set_ts));
	set_ts.timestamp = orig_ts;
	rc = cxlmi_cmd_set_timestamp(ep, NULL, &set_ts);
	if (rc) {
		if (rc > 0)
			printf("set_timestamp error: %s\n",
			       cxlmi_cmd_retcode_tostr(rc));
		return rc;
	}

	memset(&get_ts, 0, sizeof(get_ts));
	rc = cxlmi_cmd_get_timestamp(ep, NULL, &get_ts);
	if (rc)
		return rc;
	printf("reset back to original device timestamp: %lu\n", get_ts.timestamp);

	return 0;
}

static const uint8_t cel_uuid[0x10] = { 0x0d, 0xa9, 0xc0, 0xb5,
					0xbf, 0x41,
					0x4b, 0x78,
					0x8f, 0x79,
					0x96, 0xb1, 0x62, 0x3b, 0x3f, 0x17 };

static const uint8_t ven_dbg[0x10] = { 0x5e, 0x18, 0x19, 0xd9,
				       0x11, 0xa9,
				       0x40, 0x0c,
				       0x81, 0x1f,
				       0xd6, 0x07, 0x19, 0x40, 0x3d, 0x86 };

static const uint8_t c_s_dump[0x10] = { 0xb3, 0xfa, 0xb4, 0xcf,
					0x01, 0xb6,
					0x43, 0x32,
					0x94, 0x3e,
					0x5e, 0x99, 0x62, 0xf2, 0x35, 0x67 };

static int parse_supported_logs(struct cxlmi_cmd_get_supported_logs *pl,
				size_t *cel_size)
{
	int i, j;

	*cel_size = 0;
	printf("Get Supported Logs Response %d\n",
	       pl->num_supported_log_entries);

	for (i = 0; i < pl->num_supported_log_entries; i++) {
		for (j = 0; j < sizeof(pl->entries[i].uuid); j++) {
			if (pl->entries[i].uuid[j] != cel_uuid[j])
				break;
		}
		if (j == 0x10) {
			*cel_size = pl->entries[i].log_size;
			printf("\tCommand Effects Log (CEL) available\n");
		}
		for (j = 0; j < sizeof(pl->entries[i].uuid); j++) {
			if (pl->entries[i].uuid[j] != ven_dbg[j])
				break;
		}
		if (j == 0x10)
			printf("\tVendor Debug Log available\n");
		for (j = 0; j < sizeof(pl->entries[i].uuid); j++) {
			if (pl->entries[i].uuid[j] != c_s_dump[j])
				break;
		}
		if (j == 0x10)
			printf("\tComponent State Dump Log available\n");
	}
	if (cel_size == 0) {
		return -1;
	}
	return 0;
}

static int show_cel(struct cxlmi_endpoint *ep, int cel_size)
{
	struct cxlmi_cmd_get_log_req in = {
		.offset = 0,
		.length = cel_size,
	};
	struct cxlmi_cmd_get_log_cel_rsp *ret;
	int i, rc;

	ret = calloc(1, sizeof(*ret) + cel_size);
	if (!ret)
		return -1;

	memcpy(in.uuid, cel_uuid, sizeof(in.uuid));
	rc = cxlmi_cmd_get_log_cel(ep, NULL, &in, ret);
	if (rc)
		goto done;

	for (i = 0; i < cel_size / sizeof(*ret); i++) {
		printf("\t[%04x] %s%s%s%s%s%s%s%s\n",
		       ret[i].opcode,
		       ret[i].command_effect & 0x1 ? "ColdReset " : "",
		       ret[i].command_effect & 0x2 ? "ImConf " : "",
		       ret[i].command_effect & 0x4 ? "ImData " : "",
		       ret[i].command_effect & 0x8 ? "ImPol " : "",
		       ret[i].command_effect & 0x10 ? "ImLog " : "",
		       ret[i].command_effect & 0x20 ? "ImSec" : "",
		       ret[i].command_effect & 0x40 ? "BgOp" : "",
		       ret[i].command_effect & 0x80 ? "SecSup" : "");
	}
done:
	free(ret);
	return rc;
}

static int get_device_logs(struct cxlmi_endpoint *ep)
{
	int rc;
	size_t cel_size;
	struct cxlmi_cmd_get_supported_logs *gsl;

	gsl = calloc(1, sizeof(*gsl) +
		     CXLMI_MAX_SUPPORTED_LOGS * sizeof(*gsl->entries));
	if (!gsl)
		return -1;

	rc = cxlmi_cmd_get_supported_logs(ep, NULL, gsl);
	if (rc)
		return rc;

	rc = parse_supported_logs(gsl, &cel_size);
	if (rc)
		return rc;
	else {
		/* we know there is a CEL */
		rc = show_cel(ep, cel_size);
	}

	free(gsl);
	return rc;
}

int main(int argc, char **argv)
{
	struct cxlmi_ctx *ctx;
	struct cxlmi_endpoint *ep;
	int rc = EXIT_FAILURE;

	if (argc != 4 || strcmp(argv[2], "-d") != 0)
    {
        printf("Usage: %s <BDF> -d <debug_mode>\n", argv[0]);
        return 1;
    }
    
    debug_mode = atoi(argv[3]);
    if (debug_mode == 1)
    {
        printf("Debug mode enabled\n");
    }
    else
    {
        printf("Debug mode disabled\n");
    }



	ctx = cxlmi_new_ctx(stdout, DEFAULT_LOGLEVEL);
	if (!ctx) {
		fprintf(stderr, "cannot create new context object\n");
		goto exit;
	}

	ep = cxlmi_config_space_access(ctx, argv[1]);
	if (!ep) {
		fprintf(stderr, "cannot open '%s' endpoint\n", argv[1]);
		goto exit_free_ctx;
	}

	printf("ep '%s'\n", argv[1]);

	/* yes, only 1 endpoint, but might add more */
	rc = show_some_info_from_all_devices(ctx);

	rc = play_with_device_timestamp(ep);

	rc = get_device_logs(ep);

	rc = toggle_abort(ep);

	cxlmi_close(ep);
exit_free_ctx:
	cxlmi_free_ctx(ctx);
exit:
	return rc;
}
