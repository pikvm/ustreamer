#include <unistd.h>

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>

#include "../logging.h"

#include "formatters.h"
#include "component.h"


static int _component_wait_port_changed(OMX_HANDLETYPE *component, const OMX_U32 port, const OMX_BOOL enabled);
static int _component_wait_state_changed(OMX_HANDLETYPE *component, const OMX_STATETYPE wanted);


int component_enable_port(OMX_HANDLETYPE *component, const OMX_U32 port) {
	OMX_ERRORTYPE error;

	LOG_DEBUG("Enabling OMX port %u ...", port);
	if ((error = OMX_SendCommand(*component, OMX_CommandPortEnable, port, NULL)) != OMX_ErrorNone) {
		LOG_OMX_ERROR(error, "Can't enable OMX port %u", port);
		return -1;
	}
	return _component_wait_port_changed(component, port, OMX_TRUE);
}

int component_disable_port(OMX_HANDLETYPE *component, const OMX_U32 port) {
	OMX_ERRORTYPE error;

	LOG_DEBUG("Disabling OMX port %u ...", port);
	if ((error = OMX_SendCommand(*component, OMX_CommandPortDisable, port, NULL)) != OMX_ErrorNone) {
		LOG_OMX_ERROR(error, "Can't disable OMX port %u", port);
		return -1;
	}
	return _component_wait_port_changed(component, port, OMX_FALSE);
}

int component_get_portdef(OMX_HANDLETYPE *component, OMX_PARAM_PORTDEFINITIONTYPE *portdef, const OMX_U32 port) {
	OMX_ERRORTYPE error;

	OMX_INIT_STRUCTURE(*portdef);
	portdef->nPortIndex = port;

	LOG_DEBUG("Fetching OMX port %u definition ...", port);
	if ((error = OMX_GetParameter(*component, OMX_IndexParamPortDefinition, portdef)) != OMX_ErrorNone) {
		LOG_OMX_ERROR(error, "Can't get OMX port %u definition", port);
		return -1;
	}
	return 0;
}

int component_set_portdef(OMX_HANDLETYPE *component, OMX_PARAM_PORTDEFINITIONTYPE *portdef) {
	OMX_ERRORTYPE error;

	LOG_DEBUG("Writing OMX port %u definition ...", port);
	if ((error = OMX_SetParameter(*component, OMX_IndexParamPortDefinition, portdef)) != OMX_ErrorNone) {
		LOG_OMX_ERROR(error, "Can't set OMX port %u definition", portdef->nPortIndex);
		return -1;
	}
	return 0;
}

int component_set_state(OMX_HANDLETYPE *component, const OMX_STATETYPE state) {
	OMX_ERRORTYPE error;

	LOG_DEBUG("Switching component state to %s ...", omx_state_to_string(state));
	if ((error = OMX_SendCommand(*component, OMX_CommandStateSet, state, NULL)) != OMX_ErrorNone) {
		//if (error == OMX_ErrorSameState) {
		//	return 0;
		//}
		LOG_OMX_ERROR(error, "Can't switch OMX component state to %s", omx_state_to_string(state));
		return -1;
	}
	return _component_wait_state_changed(component, state);
}


static int _component_wait_port_changed(OMX_HANDLETYPE *component, const OMX_U32 port, const OMX_BOOL enabled) {
	OMX_ERRORTYPE error;
	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	int retries = 50;

	OMX_INIT_STRUCTURE(portdef);
	portdef.nPortIndex = port;

	do {
		if ((error = OMX_GetParameter(*component, OMX_IndexParamPortDefinition, &portdef)) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Can't get OMX port %u definition for waiting", port);
			return -1;
		}

		if (portdef.bEnabled != enabled) {
			LOG_DEBUG("Waiting for OMX %s port %u", (enabled ? "enabling" : "disabling"), port);
			retries -= 1;
			usleep(8000);
		}
	} while (portdef.bEnabled != enabled && retries);

	LOG_DEBUG("OMX port %u %s", port, (enabled ? "enabled" : "disabled"));
	return (portdef.bEnabled == enabled ? 0 : -1);
}

static int _component_wait_state_changed(OMX_HANDLETYPE *component, const OMX_STATETYPE wanted) {
	OMX_ERRORTYPE error;
	OMX_STATETYPE state;
	int retries = 50;

	do {
		if ((error = OMX_GetState(*component, &state)) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Failed to get OMX component state");
			return -1;
		}

		if (state != wanted) {
			LOG_DEBUG("Waiting when OMX component state changes to %s", omx_state_to_string(wanted));
			retries -= 1;
			usleep(8000);
		}
	} while (state != wanted && retries);

	LOG_DEBUG("Switched OMX component state to %s", omx_state_to_string(wanted))
	return (state == wanted ? 0 : -1);
}
