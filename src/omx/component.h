#pragma once

#include <string.h>

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>


#define OMX_INIT_STRUCTURE(_var) { \
		memset(&(_var), 0, sizeof(_var)); \
		(_var).nSize = sizeof(_var); \
		(_var).nVersion.nVersion = OMX_VERSION; \
		(_var).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
		(_var).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
		(_var).nVersion.s.nRevision = OMX_VERSION_REVISION; \
		(_var).nVersion.s.nStep = OMX_VERSION_STEP; \
	}


int component_enable_port(OMX_HANDLETYPE *component, const OMX_U32 port);
int component_disable_port(OMX_HANDLETYPE *component, const OMX_U32 port);
int component_get_portdef(OMX_HANDLETYPE *component, OMX_PARAM_PORTDEFINITIONTYPE *portdef, const OMX_U32 port);
int component_set_portdef(OMX_HANDLETYPE *component, OMX_PARAM_PORTDEFINITIONTYPE *portdef);
int component_set_state(OMX_HANDLETYPE *component, const OMX_STATETYPE state);
