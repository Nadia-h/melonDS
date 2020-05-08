#ifndef __SDL_CONFPATH_H
#define __SDL_CONFPATH_H

extern const char* conf_dir_name;
extern char* conf_path;

bool setup_config_path(const char* custom_path);

#endif
