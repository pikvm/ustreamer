/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2024  Maxim Devaev <mdevaev@gmail.com>               #
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


#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <janus/config.h>
#include <janus/plugins/plugin.h>

#include "uslibs/tools.h"

#include "const.h"
#include "logging.h"


static char *_get_value(janus_config *jcfg, const char *section, const char *option);
static uint _get_uint(janus_config *jcfg, const char *section, const char *option, bool def);
// static bool _get_bool(janus_config *jcfg, const char *section, const char *option, bool def);


us_config_s *us_config_init(const char *config_dir_path) {
	us_config_s *config;
	US_CALLOC(config, 1);

	char *config_file_path;
	janus_config *jcfg = NULL;

	US_ASPRINTF(config_file_path, "%s/%s.jcfg", config_dir_path, US_PLUGIN_PACKAGE);
	US_JLOG_INFO("config", "Reading config file '%s' ...", config_file_path);

	jcfg = janus_config_parse(config_file_path);
	if (jcfg == NULL) {
		US_JLOG_ERROR("config", "Can't read config");
		goto error;
	}
	janus_config_print(jcfg);

	if ((config->video_sink_name = _get_value(jcfg, "video", "sink")) == NULL) {
		US_JLOG_ERROR("config", "Missing config value: video.sink");
		goto error;
	}
	if ((config->acap_dev_name = _get_value(jcfg, "acap", "device")) != NULL) {
		if ((config->tc358743_dev_path = _get_value(jcfg, "acap", "tc358743")) == NULL) {
			US_JLOG_INFO("config", "Missing config value: acap.tc358743");
			goto error;
		}
		config->acap_sampling_rate = _get_uint(jcfg, "acap", "sampling_rate", 0);
		if ((config->aplay_dev_name = _get_value(jcfg, "aplay", "device")) != NULL) {
			char *path = _get_value(jcfg, "aplay", "check");
			if (path != NULL) {
				if (access(path, F_OK) != 0) {
					US_JLOG_INFO("config", "No check file found, aplay will be disabled");
					US_DELETE(config->aplay_dev_name, free);
				}
				US_DELETE(path, free);
			}
		}
	}

	goto ok;

error:
	US_DELETE(config, us_config_destroy);

ok:
	US_DELETE(jcfg, janus_config_destroy);
	free(config_file_path);
	return config;
}

void us_config_destroy(us_config_s *config) {
	US_DELETE(config->video_sink_name, free);
	US_DELETE(config->acap_dev_name, free);
	US_DELETE(config->tc358743_dev_path, free);
	US_DELETE(config->aplay_dev_name, free);
	free(config);
}

static char *_get_value(janus_config *jcfg, const char *section, const char *option) {
	janus_config_category *section_obj = janus_config_get_create(jcfg, NULL, janus_config_type_category, section);
	janus_config_item *option_obj = janus_config_get(jcfg, section_obj, janus_config_type_item, option);
	if (option_obj == NULL || option_obj->value == NULL || option_obj->value[0] == '\0') {
		return NULL;
	}
	return us_strdup(option_obj->value);
}

static uint _get_uint(janus_config *jcfg, const char *section, const char *option, bool def) {
	char *const tmp = _get_value(jcfg, section, option);
	uint value = def;
	if (tmp != NULL) {
		errno = 0;
		value = (uint) strtoul(tmp, NULL, 10);
		if (errno != 0) {
			value = def;
		}
		free(tmp);
	}
	return value;
}

/*static bool _get_bool(janus_config *jcfg, const char *section, const char *option, bool def) {
	char *const tmp = _get_value(jcfg, section, option);
	bool value = def;
	if (tmp != NULL) {
		value = (!strcasecmp(tmp, "1") || !strcasecmp(tmp, "true") || !strcasecmp(tmp, "yes"));
		free(tmp);
	}
	return value;
}*/
