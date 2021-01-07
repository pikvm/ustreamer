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


#include "formatters.h"


#define CASE_TO_STRING(_value) \
	case _value: { return #_value; }

const char *omx_error_to_string(OMX_ERRORTYPE error) {
	switch (error) {
		CASE_TO_STRING(OMX_ErrorNone);
		CASE_TO_STRING(OMX_ErrorInsufficientResources);
		CASE_TO_STRING(OMX_ErrorUndefined);
		CASE_TO_STRING(OMX_ErrorInvalidComponentName);
		CASE_TO_STRING(OMX_ErrorComponentNotFound);
		CASE_TO_STRING(OMX_ErrorInvalidComponent);
		CASE_TO_STRING(OMX_ErrorBadParameter);
		CASE_TO_STRING(OMX_ErrorNotImplemented);
		CASE_TO_STRING(OMX_ErrorUnderflow);
		CASE_TO_STRING(OMX_ErrorOverflow);
		CASE_TO_STRING(OMX_ErrorHardware);
		CASE_TO_STRING(OMX_ErrorInvalidState);
		CASE_TO_STRING(OMX_ErrorStreamCorrupt);
		CASE_TO_STRING(OMX_ErrorPortsNotCompatible);
		CASE_TO_STRING(OMX_ErrorResourcesLost);
		CASE_TO_STRING(OMX_ErrorNoMore);
		CASE_TO_STRING(OMX_ErrorVersionMismatch);
		CASE_TO_STRING(OMX_ErrorNotReady);
		CASE_TO_STRING(OMX_ErrorTimeout);
		CASE_TO_STRING(OMX_ErrorSameState);
		CASE_TO_STRING(OMX_ErrorResourcesPreempted);
		CASE_TO_STRING(OMX_ErrorPortUnresponsiveDuringAllocation);
		CASE_TO_STRING(OMX_ErrorPortUnresponsiveDuringDeallocation);
		CASE_TO_STRING(OMX_ErrorPortUnresponsiveDuringStop);
		CASE_TO_STRING(OMX_ErrorIncorrectStateTransition);
		CASE_TO_STRING(OMX_ErrorIncorrectStateOperation);
		CASE_TO_STRING(OMX_ErrorUnsupportedSetting);
		CASE_TO_STRING(OMX_ErrorUnsupportedIndex);
		CASE_TO_STRING(OMX_ErrorBadPortIndex);
		CASE_TO_STRING(OMX_ErrorPortUnpopulated);
		CASE_TO_STRING(OMX_ErrorComponentSuspended);
		CASE_TO_STRING(OMX_ErrorDynamicResourcesUnavailable);
		CASE_TO_STRING(OMX_ErrorMbErrorsInFrame);
		CASE_TO_STRING(OMX_ErrorFormatNotDetected);
		CASE_TO_STRING(OMX_ErrorContentPipeOpenFailed);
		CASE_TO_STRING(OMX_ErrorContentPipeCreationFailed);
		CASE_TO_STRING(OMX_ErrorSeperateTablesUsed);
		CASE_TO_STRING(OMX_ErrorTunnelingUnsupported);
		CASE_TO_STRING(OMX_ErrorKhronosExtensions);
		CASE_TO_STRING(OMX_ErrorVendorStartUnused);
		CASE_TO_STRING(OMX_ErrorDiskFull);
		CASE_TO_STRING(OMX_ErrorMaxFileSize);
		CASE_TO_STRING(OMX_ErrorDrmUnauthorised);
		CASE_TO_STRING(OMX_ErrorDrmExpired);
		CASE_TO_STRING(OMX_ErrorDrmGeneral);
		default: return "Unknown OMX error";
	}
}

const char *omx_state_to_string(OMX_STATETYPE state) {
	switch (state) {
		CASE_TO_STRING(OMX_StateInvalid);
		CASE_TO_STRING(OMX_StateLoaded);
		CASE_TO_STRING(OMX_StateIdle);
		CASE_TO_STRING(OMX_StateExecuting);
		CASE_TO_STRING(OMX_StatePause);
		CASE_TO_STRING(OMX_StateWaitForResources);
		// cppcheck-suppress constArgument
		// cppcheck-suppress knownArgument
		default: break;
	}
	assert(0 && "Unsupported OMX state");
}

#undef CASE_TO_STRING
