/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018-2021  Maxim Devaev <mdevaev@gmail.com>               #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
*****************************************************************************/


#include "component.h"


static int _omx_component_wait_port_changed(OMX_HANDLETYPE *comp, OMX_U32 port, OMX_BOOL enabled);
static int _omx_component_wait_state_changed(OMX_HANDLETYPE *comp, OMX_STATETYPE wanted);


int omx_component_enable_port(OMX_HANDLETYPE *comp, OMX_U32 port) {
	OMX_ERRORTYPE error;

	LOG_DEBUG("Enabling OMX port %u ...", port);
	if ((error = OMX_SendCommand(*comp, OMX_CommandPortEnable, port, NULL)) != OMX_ErrorNone) {
		LOG_ERROR_OMX(error, "Can't enable OMX port %u", port);
		return -1;
	}
	return _omx_component_wait_port_changed(comp, port, OMX_TRUE);
}

int omx_component_disable_port(OMX_HANDLETYPE *comp, OMX_U32 port) {
	OMX_ERRORTYPE error;

	LOG_DEBUG("Disabling OMX port %u ...", port);
	if ((error = OMX_SendCommand(*comp, OMX_CommandPortDisable, port, NULL)) != OMX_ErrorNone) {
		LOG_ERROR_OMX(error, "Can't disable OMX port %u", port);
		return -1;
	}
	return _omx_component_wait_port_changed(comp, port, OMX_FALSE);
}

int omx_component_get_portdef(OMX_HANDLETYPE *comp, OMX_PARAM_PORTDEFINITIONTYPE *portdef, OMX_U32 port) {
	OMX_ERRORTYPE error;

	// cppcheck-suppress redundantPointerOp
	OMX_INIT_STRUCTURE(*portdef);
	portdef->nPortIndex = port;

	LOG_DEBUG("Fetching OMX port %u definition ...", port);
	if ((error = OMX_GetParameter(*comp, OMX_IndexParamPortDefinition, portdef)) != OMX_ErrorNone) {
		LOG_ERROR_OMX(error, "Can't get OMX port %u definition", port);
		return -1;
	}
	return 0;
}

int omx_component_set_portdef(OMX_HANDLETYPE *comp, OMX_PARAM_PORTDEFINITIONTYPE *portdef) {
	OMX_ERRORTYPE error;

	LOG_DEBUG("Writing OMX port %u definition ...", portdef->nPortIndex);
	if ((error = OMX_SetParameter(*comp, OMX_IndexParamPortDefinition, portdef)) != OMX_ErrorNone) {
		LOG_ERROR_OMX(error, "Can't set OMX port %u definition", portdef->nPortIndex);
		return -1;
	}
	return 0;
}

int omx_component_set_state(OMX_HANDLETYPE *comp, OMX_STATETYPE state) {
	const char *state_str = omx_state_to_string(state);
	OMX_ERRORTYPE error;

	LOG_DEBUG("Switching component state to %s ...", state_str);

	int retries = 50;
	do {
		error = OMX_SendCommand(*comp, OMX_CommandStateSet, state, NULL);
		if (error == OMX_ErrorNone) {
			return _omx_component_wait_state_changed(comp, state);
		} else if (error == OMX_ErrorInsufficientResources && retries) {
			// Иногда железо не инициализируется, хз почему, просто ретраим, со второй попытки сработает
			if (retries > 45) {
				LOG_VERBOSE("Can't switch OMX component state to %s, need to retry: %s",
					state_str, omx_error_to_string(error));
			} else {
				LOG_ERROR_OMX(error, "Can't switch OMX component state to %s, need to retry", state_str);
			}
			retries -= 1;
			usleep(8000);
		} else {
			break;
		}
	} while (retries);

	LOG_ERROR_OMX(error, "Can't switch OMX component state to %s", state_str);
	return -1;
}


static int _omx_component_wait_port_changed(OMX_HANDLETYPE *comp, OMX_U32 port, OMX_BOOL enabled) {
	OMX_ERRORTYPE error;

	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	OMX_INIT_STRUCTURE(portdef);
	portdef.nPortIndex = port;

	int retries = 50;
	do {
		if ((error = OMX_GetParameter(*comp, OMX_IndexParamPortDefinition, &portdef)) != OMX_ErrorNone) {
			LOG_ERROR_OMX(error, "Can't get OMX port %u definition for waiting", port);
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

static int _omx_component_wait_state_changed(OMX_HANDLETYPE *comp, OMX_STATETYPE wanted) {
	OMX_ERRORTYPE error;
	OMX_STATETYPE state;

	int retries = 50;
	do {
		if ((error = OMX_GetState(*comp, &state)) != OMX_ErrorNone) {
			LOG_ERROR_OMX(error, "Failed to get OMX component state");
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
