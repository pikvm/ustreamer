/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
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


static char *_get_value(janus_config *config, const char *section, const char *option);


int read_config(const char *config_dir_path, char **video_sink_name, char **audio_dev_name, char **tc358743_dev_path) {
	int retval = 0;

	char *config_file_path;
	janus_config *config = NULL;

	A_ASPRINTF(config_file_path, "%s/%s.jcfg", config_dir_path, PLUGIN_PACKAGE);
	JLOG_INFO("config", "Reading config file '%s' ...", config_file_path);

	config = janus_config_parse(config_file_path);
	if (config == NULL) {
		JLOG_ERROR("config", "Can't read config");
		goto error;
	}
	janus_config_print(config);

	if (
		(*video_sink_name = _get_value(config, "memsink", "object")) == NULL
		&& (*video_sink_name = _get_value(config, "video", "sink")) == NULL
	) {
		JLOG_ERROR("config", "Missing config value: video.sink (ex. memsink.object)");
		goto error;
	}
	if ((*audio_dev_name = _get_value(config, "audio", "device")) != NULL) {
		JLOG_INFO("config", "Enabled the experimental AUDIO feature");
		if ((*tc358743_dev_path = _get_value(config, "audio", "tc358743")) == NULL) {
			JLOG_INFO("config", "Missing config value: audio.tc358743");
			goto error;
		}
	}

	goto ok;
	error:
		retval = -1;
	ok:
		if (config) {
			janus_config_destroy(config);
		}
		free(config_file_path);
		return retval;
}

static char *_get_value(janus_config *config, const char *section, const char *option) {
	janus_config_category *section_obj = janus_config_get_create(config, NULL, janus_config_type_category, section);
	janus_config_item *option_obj = janus_config_get(config, section_obj, janus_config_type_item, option);
	if (option_obj == NULL || option_obj->value == NULL || option_obj->value[0] == '\0') {
		return NULL;
	}
	return strdup(option_obj->value);
}
