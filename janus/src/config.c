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


static char *_get_value(janus_config *jcfg, const char *section, const char *option);


plugin_config_s *plugin_config_init(const char *config_dir_path) {
	plugin_config_s *config;
	A_CALLOC(config, 1);

	char *config_file_path;
	janus_config *jcfg = NULL;

	A_ASPRINTF(config_file_path, "%s/%s.jcfg", config_dir_path, PLUGIN_PACKAGE);
	JLOG_INFO("config", "Reading config file '%s' ...", config_file_path);

	jcfg = janus_config_parse(config_file_path);
	if (jcfg == NULL) {
		JLOG_ERROR("config", "Can't read config");
		goto error;
	}
	janus_config_print(jcfg);

	if (
		(config->video_sink_name = _get_value(jcfg, "memsink", "object")) == NULL
		&& (config->video_sink_name = _get_value(jcfg, "video", "sink")) == NULL
	) {
		JLOG_ERROR("config", "Missing config value: video.sink (ex. memsink.object)");
		goto error;
	}
	if ((config->audio_dev_name = _get_value(jcfg, "audio", "device")) != NULL) {
		JLOG_INFO("config", "Enabled the experimental AUDIO feature");
		if ((config->tc358743_dev_path = _get_value(jcfg, "audio", "tc358743")) == NULL) {
			JLOG_INFO("config", "Missing config value: audio.tc358743");
			goto error;
		}
	}

	goto ok;
	error:
		plugin_config_destroy(config);
		config = NULL;
	ok:
		if (jcfg) {
			janus_config_destroy(jcfg);
		}
		free(config_file_path);
		return config;
}

void plugin_config_destroy(plugin_config_s *config) {
	DELETE(config->video_sink_name, free);
	DELETE(config->audio_dev_name, free);
	DELETE(config->tc358743_dev_path, free);
	free(config);
}

static char *_get_value(janus_config *jcfg, const char *section, const char *option) {
	janus_config_category *section_obj = janus_config_get_create(jcfg, NULL, janus_config_type_category, section);
	janus_config_item *option_obj = janus_config_get(jcfg, section_obj, janus_config_type_item, option);
	if (option_obj == NULL || option_obj->value == NULL || option_obj->value[0] == '\0') {
		return NULL;
	}
	return strdup(option_obj->value);
}
