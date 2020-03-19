#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "ConfPath.h"

const char* conf_dir_name = "melonDS-sdl/";
char* conf_path;

bool create_conf_path() {
	char* p;

	for (p = conf_path + 1; *p != '\0'; p++) {
		if (*p == '/') {
			*p = '\0';
			if (access(conf_path, F_OK) == -1) {
				if (mkdir(conf_path, 0755) == -1) {
					perror(conf_path);
					return false;
				}
			}
			*p = '/';
		}
	}

	return true;
}

bool setup_config_path(const char* custom_path) {
	const char* xdg_config_home = getenv("XDG_CONFIG_HOME");
	const char* home = getenv("HOME");

	std::string path;
	std::string separator = "/";

	if (custom_path != NULL) {
		path += custom_path;
	} else if (xdg_config_home != NULL) {
		path += std::string(xdg_config_home)
			+ separator
			+ std::string(conf_dir_name);
	} else if (home != NULL) {
		path += std::string(home)
			+ separator
			+ std::string(".config/")
			+ std::string(conf_dir_name);
	} else {
		path += "./";
	}

	if (path.at(path.length() - 1) != '/') {
		path += '/';
	}

	conf_path = new char[path.length() + 1];
	strcpy(conf_path, path.c_str());

	if (access(conf_path, F_OK) == 0) {
		return true;
	}

	return create_conf_path();
}
