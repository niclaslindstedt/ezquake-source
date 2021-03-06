/*

Copyright (C) 2001-2002       A Nourai

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the included (GNU.txt) GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef _CONFIG_MANAGER_H_

#define _CONFIG_MANAGER_H_

void ConfigManager_Init (void);
void Config_QuitSave(void);
void ResetBinds(void);
void Config_ExecIfExists(char* filename);

extern cvar_t	cfg_save_unchanged, cfg_legacy_exec;

// Defined in order of execution
#define PACKAGE_CONFIG_FILENAME "configs/__package.cfg"   // package configuration (e.g. nQuake)
#define PLATFORM_CONFIG_FILENAME "configs/__platform.cfg" // platform configuration (e.g. Linux-specific settings)
#define PRESET_CONFIG_FILENAME "configs/__preset.cfg"     // preset configuration (e.g. auto-generated per-user settings)
#define GLOBAL_CONFIG_FILENAME "configs/__global.cfg"     // per-user configuration (executed per-mod)
#define DEFAULT_CONFIG_FILENAME "default.cfg"             // default per-mod configuration
#define AUTOEXEC_CONFIG_FILENAME "autoexec.cfg"           // per-user configuration (executed per-mod)
#define STARTUP_CONFIG_FILENAME "configs/__startup.cfg"   // startup configuration (e.g. welcome message)

#define MAIN_GL_CONFIG_FILENAME "config.cfg"
#define MAIN_SW_CONFIG_FILENAME "configsw.cfg"
#define MAIN_CONFIG_FILENAME MAIN_GL_CONFIG_FILENAME

#endif
