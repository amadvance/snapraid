/*
 * Copyright (C) 2011 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "portable.h"

#include "elem.h"
#include "import.h"
#include "state.h"
#include "util.h"
#include "parity.h"
#include "raid/raid.h"
#include "raid/cpu.h"

const char* lev_name(unsigned l)
{
	switch (l) {
	case 0 : return "Parity";
	case 1 : return "2-Parity";
	case 2 : return "3-Parity";
	case 3 : return "4-Parity";
	case 4 : return "5-Parity";
	case 5 : return "6-Parity";
	}

	return 0;
}

const char* lev_config_name(unsigned l)
{
	switch (l) {
	case 0 : return "parity";
	case 1 : return "2-parity";
	case 2 : return "3-parity";
	case 3 : return "4-parity";
	case 4 : return "5-parity";
	case 5 : return "6-parity";
	}

	return 0;
}

const char* lev_raid_name(unsigned mode, unsigned n)
{
	switch (n) {
	case 1 : return "par1";
	case 2 : return "par2";
	case 3 : if (mode == RAID_MODE_CAUCHY)
			return "par3";
		else
			return "parz";
	case 4 : return "par4";
	case 5 : return "par5";
	case 6 : return "par6";
	}

	return 0;
}

void state_init(struct snapraid_state* state)
{
	unsigned l;

	memset(&state->opt, 0, sizeof(state->opt));
	state->filter_hidden = 0;
	state->autosave = 0;
	state->need_write = 0;
	state->block_size = 256 * 1024; /* default 256 KiB */
	state->raid_mode = RAID_MODE_CAUCHY;
	for(l=0;l<LEV_MAX;++l) {
		state->parity_path[l][0] = 0;
		state->parity_device[l] = 0;
	}
	state->share[0] = 0;
	state->pool[0] = 0;
	state->pool_device = 0;
	state->lockfile[0] = 0;
	state->level = 1; /* default is the lowest protection */
	state->loaded_paritymax = 0;
	state->clear_undeterminate_hash = 0;
	state->no_conf = 0;

	tommy_list_init(&state->disklist);
	tommy_list_init(&state->maplist);
	tommy_list_init(&state->contentlist);
	tommy_list_init(&state->filterlist);
	tommy_list_init(&state->importlist);
	tommy_hashdyn_init(&state->importset);
	tommy_hashdyn_init(&state->previmportset);
	tommy_arrayblkof_init(&state->infoarr, sizeof(snapraid_info));
}

void state_done(struct snapraid_state* state)
{
	tommy_list_foreach(&state->disklist, (tommy_foreach_func*)disk_free);
	tommy_list_foreach(&state->maplist, (tommy_foreach_func*)map_free);
	tommy_list_foreach(&state->contentlist, (tommy_foreach_func*)content_free);
	tommy_list_foreach(&state->filterlist, (tommy_foreach_func*)filter_free);
	tommy_list_foreach(&state->importlist, (tommy_foreach_func*)import_file_free);
	tommy_hashdyn_done(&state->importset);
	tommy_hashdyn_done(&state->previmportset);
	tommy_arrayblkof_done(&state->infoarr);
}

/**
 * Checks the configuration.
 */
static void state_config_check(struct snapraid_state* state, const char* path, tommy_list* filterlist_disk)
{
	tommy_node* i;
	unsigned l;

	/* check for parity level */
	if (state->raid_mode == RAID_MODE_VANDERMONDE) {
		if (state->level > 3) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "If you use the z-parity you cannot have more than 3 parities.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STIO */
		}
	}

	for(l=0;l<state->level;++l) {
		if (state->parity_path[l][0] == 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "No '%s' specification in '%s'\n", lev_config_name(l), path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	if (tommy_list_empty(&state->contentlist)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "No 'content' specification in '%s'\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* checks for equal paths */
	for(i=state->contentlist;i!=0;i=i->next) {
		struct snapraid_content* content = i->data;

		for(l=0;l<state->level;++l) {
			if (pathcmp(state->parity_path[l], content->content) == 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Same path used for '%s' and 'content' as '%s'\n", lev_config_name(l), content->content);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/* check if all the data and parity disks are different */
	if (!state->opt.skip_device) {
		for(i=state->disklist;i!=0;i=i->next) {
			tommy_node* j;
			struct snapraid_disk* disk = i->data;

#ifdef _WIN32
			if (disk->device == 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Disk '%s' has a zero serial number.\n", disk->dir);
				fprintf(stderr, "This is not necessarely wrong, but for using SnapRAID\n");
				fprintf(stderr, "it's better to change the serial number of the disk.\n");
				fprintf(stderr, "Try using the 'VolumeID' tool by 'Mark Russinovich'.\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
#endif

			if (!state->opt.force_device) {
				for(j=i->next;j!=0;j=j->next) {
					struct snapraid_disk* other = j->data;
					if (disk->device == other->device) {
						/* LCOV_EXCL_START */
						fprintf(stderr, "Disks '%s' and '%s' are on the same device.\n", disk->dir, other->dir);
#ifdef _WIN32
						fprintf(stderr, "Both have the serial number '%"PRIx64"'.\n", disk->device);
#endif
						/* in "fix" we allow to continue anyway */
						if (strcmp(state->command, "fix") == 0) {
							fprintf(stderr, "You can '%s' anyway, using 'snapraid --force-device %s'.\n", state->command, state->command);
						}
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}
				}
			}

			for(l=0;l<state->level;++l) {
				if (disk->device == state->parity_device[l]) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Disk '%s' and %s '%s' are on the same device.\n", disk->dir, lev_name(l), state->parity_path[l]);
#ifdef _WIN32
					fprintf(stderr, "Both have the serial number '%"PRIx64"'.\n", disk->device);
#endif
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}

			if (state->pool[0] != 0 && disk->device == state->pool_device) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Disk '%s' and pool '%s' are on the same device.\n", disk->dir, state->pool);
#ifdef _WIN32
				fprintf(stderr, "Both have the serial number '%"PRIx64"'.\n", disk->device);
#endif
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

#ifdef _WIN32
		for(l=0;l<state->level;++l) {
			if (state->parity_device[l] == 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Disk '%s' has a zero serial number.\n", state->parity_path[l]);
				fprintf(stderr, "This is not necessarely wrong, but for using SnapRAID\n");
				fprintf(stderr, "it's better to change the serial number of the disk.\n");
				fprintf(stderr, "Try using the 'VolumeID' tool by 'Mark Russinovich'.\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		if (state->pool[0] != 0 && state->pool_device == 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Disk '%s' has a zero serial number.\n", state->pool);
			fprintf(stderr, "This is not necessarely wrong, but for using SnapRAID\n");
			fprintf(stderr, "it's better to change the serial number of the disk.\n");
			fprintf(stderr, "Try using the 'VolumeID' tool by 'Mark Russinovich'.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
#endif

		for(l=0;l<state->level;++l) {
			unsigned j;
			for(j=l+1;j<state->level;++j) {
				if (state->parity_device[l] == state->parity_device[j]) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Parity '%s' and '%s' are on the same device.\n", state->parity_path[l], state->parity_path[j]);
#ifdef _WIN32
					fprintf(stderr, "Both have the serial number '%"PRIx64"'.\n", state->parity_device[l]);
#endif			
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
		}

		for(l=0;l<state->level;++l) {
			if (state->pool[0] != 0 && state->pool_device == state->parity_device[l]) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Pool '%s' and parity '%s' are on the same device.\n", state->pool, state->parity_path[l]);
#ifdef _WIN32
				fprintf(stderr, "Both have the serial number '%"PRIx64"'.\n", state->pool_device);
#endif
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/* count the content files */
	if (!state->opt.skip_device) {
		unsigned content_count;

		content_count = 0;
		for(i=state->contentlist;i!=0;i=i->next) {
			tommy_node* j;
			struct snapraid_content* content = i->data;

			/* check if there are others in the same disk */
			for(j=i->next;j!=0;j=j->next) {
				struct snapraid_content* other = j->data;
				if (content->device == other->device) {
					break;
				}
			}
			if (j != 0) {
				/* skip it */
				continue;
			}

			++content_count;
		}

		if (content_count < state->level+1) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "You must have at least %d 'content' files in different disks.\n", state->level+1);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	/* check for speed */
#ifdef CONFIG_X86
	if (!raid_cpu_has_ssse3())
#endif
	if (state->raid_mode == RAID_MODE_CAUCHY) {
		if (state->level == 3) {
			fprintf(stderr, "WARNING! Your CPU doesn't have a fast implementation for triple parity.\n");
			fprintf(stderr, "WARNING! It's recommended to switch to 'z-parity' instead than '3-parity'.\n");
		} else if (state->level > 3) {
			fprintf(stderr, "WARNING! Your CPU doesn't have a fast implementation beyond triple parity.\n");
			fprintf(stderr, "WARNING! It's recommended to reduce the parity levels to triple parity.\n");
		}
	}

	/* ensure that specified filter disks are valid ones */
	for(i=tommy_list_head(filterlist_disk);i!=0;i=i->next) {
		tommy_node* j;
		struct snapraid_filter* filter = i->data;
		for(j=state->disklist;j!=0;j=j->next) {
			struct snapraid_disk* disk = j->data;
			if (fnmatch(filter->pattern, disk->name, FNM_CASEINSENSITIVE_FOR_WIN) == 0)
				break;
		}
		if (j == 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Option -d, --filter-disk %s doesn't match any disk.\n", filter->pattern);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	
}

void state_config(struct snapraid_state* state, const char* path, const char* command, struct snapraid_option* opt, tommy_list* filterlist_disk)
{
	STREAM* f;
	unsigned line;
	tommy_node* i;
	unsigned l;
	time_t t;
	struct tm* tm;

	/* copy the options */
	state->opt = *opt;

	/* if unsed, sort by physical order */
	if (!state->opt.force_order)
		state->opt.force_order = SORT_PHYSICAL;
	
	state->command = command;

	t = time(0);
	tm = localtime(&t);

	fprintf(stdlog, "version:%s\n", PACKAGE_VERSION);
	fprintf(stdlog, "unixtime:%"PRIi64"\n", (int64_t)t);
	if (tm) {
		char datetime[64];
		strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", tm);
		fprintf(stdlog, "time:%s\n", datetime);
	}
	fprintf(stdlog, "conf:%s\n", path);
	fflush(stdlog);

	f = sopen_read(path);
	if (!f) {
		/* LCOV_EXCL_START */
		if (errno == ENOENT) {
			fprintf(stderr, "No configuration file found at '%s'\n", path);
		} else if (errno == EACCES) {
			fprintf(stderr, "You do not have rights to access the configuration file '%s'\n", path);
		} else {
			fprintf(stderr, "Error opening the configuration file '%s'. %s.\n", path, strerror(errno));
		}
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	line = 1;
	while (1) {
		char tag[PATH_MAX];
		char buffer[PATH_MAX];
		int ret;
		int c;

		/* skip initial spaces */
		sgetspace(f);

		/* read the command */
		ret = sgettok(f, tag, sizeof(tag));
		if (ret < 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error reading the configuration file '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* skip spaces after the command */
		sgetspace(f);

		if (strcmp(tag, "blocksize") == 0
			/* block_size is the old format of the option */
			|| strcmp(tag, "block_size") == 0) {

			ret = sgetu32(f, &state->block_size);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'blocksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (state->block_size < 1) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Too small 'blocksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (state->block_size > 16*1024) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Too big 'blocksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			/* check if it's a power of 2 */
			if ((state->block_size & (state->block_size - 1)) != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Not power of 2 'blocksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			state->block_size *= 1024;
		} else if (strcmp(tag, "parity") == 0
			|| strcmp(tag, "q-parity") == 0
			|| strcmp(tag, "r-parity") == 0
			|| strcmp(tag, "1-parity") == 0
			|| strcmp(tag, "2-parity") == 0
			|| strcmp(tag, "3-parity") == 0
			|| strcmp(tag, "4-parity") == 0
			|| strcmp(tag, "5-parity") == 0
			|| strcmp(tag, "6-parity") == 0
			|| strcmp(tag, "z-parity") == 0
		) {
			char device[PATH_MAX];
			char* slash;
			unsigned l;

			switch (tag[0]) {
			case 'p' : l = 0; break;
			case 'q' : l = 1; break;
			case 'r' : l = 2; break;
			case '1' : l = 0; break;
			case '2' : l = 1; break;
			case '3' : l = 2; break;
			case '4' : l = 3; break;
			case '5' : l = 4; break;
			case '6' : l = 5; break;
			case 'z' :
				l = 2;
				state->raid_mode = RAID_MODE_VANDERMONDE;
				break;
			default:
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (*state->parity_path[l]) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Multiple '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Empty '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			pathimport(state->parity_path[l], sizeof(state->parity_path[l]), buffer);

			if (state->opt.skip_parity_access) {
				struct stat st;

				/* get the device of the directory containing the parity file */
				pathimport(device, sizeof(device), buffer);
				slash = strrchr(device, '/');
				if (slash)
					*slash = 0;
				else
					pathcpy(device, sizeof(device), ".");
				if (stat(device, &st) != 0) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Error accessing 'parity' dir '%s' specification in '%s' at line %u\n", device, path, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				state->parity_device[l] = st.st_dev;
			} else {
				/* if parity is skipped, uses a fake device */
				state->parity_device[l] = -1LL - l;
			}

			/* adjust the level */
			if (state->level < l + 1)
				state->level = l + 1;
		} else if (strcmp(tag, "share") == 0) {
			if (*state->share) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Multiple 'share' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'share' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Empty 'share' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			pathimport(state->share, sizeof(state->share), buffer);
		} else if (strcmp(tag, "pool") == 0) {
			struct stat st;

			if (*state->pool) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Multiple 'pool' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'pool' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Empty 'pool' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			pathimport(state->pool, sizeof(state->pool), buffer);

			/* get the device of the directory containing the pool tree */
			if (stat(buffer, &st) != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error accessing 'pool' dir '%s' specification in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			state->pool_device = st.st_dev;
		} else if (strcmp(tag, "content") == 0) {
			struct snapraid_content* content;
			char device[PATH_MAX];
			char* slash;
			struct stat st;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (pathcmp(buffer, "/dev/null") == 0 || pathcmp(buffer, "NUL") == 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "You cannot use the null device as 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Empty 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* check if the content file is already specified */
			for(i=state->contentlist;i!=0;i=i->next) {
				content = i->data;
				if (pathcmp(content->content, buffer) == 0)
					break;
			}
			if (i) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Duplicate 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* get the device of the directory containing the content file */
			pathimport(device, sizeof(device), buffer);
			slash = strrchr(device, '/');
			if (slash)
				*slash = 0;
			else
				pathcpy(device, sizeof(device), ".");
			if (stat(device, &st) != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error accessing 'content' dir '%s' specification in '%s' at line %u\n", device, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* set the lock file at the first content file */
			if (tommy_list_empty(&state->contentlist)) {
				pathcpy(state->lockfile, sizeof(state->lockfile), buffer);
				pathcat(state->lockfile, sizeof(state->lockfile), ".lock");
			}

			content = content_alloc(buffer, st.st_dev);

			tommy_list_insert_tail(&state->contentlist, &content->node, content);
		} else if (strcmp(tag, "disk") == 0) {
			char dir[PATH_MAX];
			char device[PATH_MAX];
			struct stat st;
			struct snapraid_disk* disk;

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'disk' name specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Empty 'disk' name specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			sgetspace(f);

			ret = sgetlasttok(f, dir, sizeof(dir));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'disk' dir specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*dir) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Empty 'disk' dir specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* get the device of the dir */
			pathimport(device, sizeof(device), dir);
			if (stat(device, &st) != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error accessing 'disk' '%s' specification in '%s' at line %u\n", dir, device, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* check if the disk name already exists */
			for(i=state->disklist;i!=0;i=i->next) {
				disk = i->data;
				if (strcmp(disk->name, buffer) == 0)
					break;
			}
			if (i) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Duplicate disk name '%s' at line %u\n", buffer, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			disk = disk_alloc(buffer, dir, st.st_dev);

			tommy_list_insert_tail(&state->disklist, &disk->node, disk);
		} else if (strcmp(tag, "nohidden") == 0) {
			state->filter_hidden = 1;
		} else if (strcmp(tag, "exclude") == 0) {
			struct snapraid_filter* filter;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'exclude' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Empty 'exclude' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			filter = filter_alloc_file(-1, buffer);
			if (!filter) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'exclude' specification '%s' in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			tommy_list_insert_tail(&state->filterlist, &filter->node, filter);
		} else if (strcmp(tag, "include") == 0) {
			struct snapraid_filter* filter;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'include' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Empty 'include' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			filter = filter_alloc_file(1, buffer);
			if (!filter) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'include' specification '%s' in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			tommy_list_insert_tail(&state->filterlist, &filter->node, filter);
		} else if (strcmp(tag, "autosave") == 0) {
			char* e;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'autosave' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Empty 'autosave' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			state->autosave = strtoul(buffer, &e, 0);

			if (!e || *e) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'autosave' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* convert to GiB */
			state->autosave *= 1024 * 1024 * 1024;
		} else if (tag[0] == 0) {
			/* allow empty lines */
		} else if (tag[0] == '#') {
			ret = sgetline(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid comment in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Invalid command '%s' in '%s' at line %u\n", tag, path, line);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* skip final spaces */
		sgetspace(f);

		/* next line */
		c = sgeteol(f);
		if (c == EOF) {
			break;
		}
		if (c != '\n') {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Extra data in '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		++line;
	}

	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error reading the configuration file '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	sclose(f);

	state_config_check(state, path, filterlist_disk);

	/* select the default hash */
	if (state->opt.force_murmur3) {
		state->besthash = HASH_MURMUR3;
	} else if (state->opt.force_spooky2) {
		state->besthash = HASH_SPOOKY2;
	} else {
#ifdef CONFIG_X86
		if (sizeof(void*) == 4 && !raid_cpu_has_slowmult())
			state->besthash = HASH_MURMUR3;
		else
			state->besthash = HASH_SPOOKY2;
#else
		if (sizeof(void*) == 4)
			state->besthash = HASH_MURMUR3;
		else
			state->besthash = HASH_SPOOKY2;
#endif
	}

	/* by default use the best hash */
	state->hash = state->besthash;

	/* by default use a random hash seed */
	randomize(state->hashseed, HASH_SIZE);

	/* no previous hash by default */
	state->prevhash = HASH_UNDEFINED;

	/* intentionally not set the prevhashseed, if used valgrind will warn about it */

	fprintf(stdlog, "blocksize:%u\n", state->block_size);
	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_disk* disk = i->data;
		fprintf(stdlog, "disk:%s:%s\n", disk->name, disk->dir);
	}

	fprintf(stdlog, "mode:%s\n", lev_raid_name(state->raid_mode, state->level));
	for(l=0;l<state->level;++l)
		fprintf(stdlog, "%s:%s\n", lev_config_name(l), state->parity_path[l]);
	if (state->pool[0] != 0)
		fprintf(stdlog, "pool:%s\n", state->pool);
	fflush(stdlog);
}

/**
 * Updates the disk mapping if required.
 */
static void state_map(struct snapraid_state* state)
{
	unsigned hole;
	tommy_node* i;
	unsigned uuid_mismatch;
	unsigned diskcount;

	/* removes all the mapping without a disk */
	/* this happens when a disk is removed from the configuration file */
	/* From SnapRAID 4.0 mappings are automatically removed if a disk is not used */
	/* when saving the content file, but we keep this code to import older content files. */
	for(i=state->maplist;i!=0;) {
		struct snapraid_map* map = i->data;
		struct snapraid_disk* disk;
		tommy_node* j;

		for(j=state->disklist;j!=0;j=j->next) {
			disk = j->data;
			if (strcmp(disk->name, map->name) == 0) {
				/* disk found */
				break;
			}
		}

		/* go to the next mapping before removing */
		i = i->next;
		
		if (j==0) {
			/* disk not found, remove the mapping */
			tommy_list_remove_existing(&state->maplist, &map->node);
			map_free(map);
		}
	}

	/* maps each unmapped disk present in the configuration file in the first available hole */
	/* this happens when you add disks for the first time in the configuration file */
	hole = 0; /* first position to try */
	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_map* map;
		tommy_node* j;

		/* check if the disk is already mapped */
		for(j=state->maplist;j!=0;j=j->next) {
			map = j->data;
			if (strcmp(disk->name, map->name) == 0) {
				/* mapping found */
				break;
			}
		}
		if (j!=0)
			continue;

		/* mapping not found, search for an hole */
		while (1) {
			for(j=state->maplist;j!=0;j=j->next) {
				map = j->data;
				if (map->position == hole) {
					/* position already used */
					break;
				}
			}
			if (j==0) {
				/* hole found */
				break;
			}

			/* try with the next one */
			++hole;
		}

		/* insert the new mapping */
		map = map_alloc(disk->name, hole, "");

		tommy_list_insert_tail(&state->maplist, &map->node, map);
	}

	/* without configuration don't check for number of data disks or uuid changes */
	if (state->no_conf)
		return;

	/* checks if mapping match the disk uuid */
	uuid_mismatch = 0;
	for(i=state->maplist;i!=0;i=i->next) {
		struct snapraid_map* map = i->data;
		struct snapraid_disk* disk;
		tommy_node* j;
		char uuid[UUID_MAX];
		int ret;

		for(j=state->disklist;j!=0;j=j->next) {
			disk = j->data;
			if (strcmp(disk->name, map->name) == 0) {
				/* disk found */
				break;
			}
		}

		if (j==0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Internal inconsistency for mapping '%s'\n", map->name);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		ret = devuuid(disk->device, uuid, sizeof(uuid));
		if (ret != 0) {
			/* uuid not available, just ignore but marks the disk with not persistent UUID */
			disk->has_different_uuid = 1;
			continue;
		}

		/* if the uuid is changed */
		if (strcmp(uuid, map->uuid) != 0) {
			/* mark the disk as with an UUID change */
			disk->has_different_uuid = 1;

			/* if the previous uuid is available */
			if (map->uuid[0] != 0) {
				/* count the number of uuid change */
				++uuid_mismatch;
				fprintf(stderr, "UUID change for disk '%s' from '%s' to '%s'\n", disk->name, map->uuid, uuid);
			} else {
				disk->had_empty_uuid = 1;
			}

			/* update the uuid in the mapping, */
			pathcpy(map->uuid, sizeof(map->uuid), uuid);

			/* write the new state with the new uuid */
			state->need_write = 1;
		}
	}

	if (!state->opt.force_uuid && uuid_mismatch > state->level) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Too many disks have UUID changed from the latest 'sync'.\n");
		fprintf(stderr, "If this happens because you really replaced them,\n");
		fprintf(stderr, "you can '%s' anyway, using 'snapraid --force-uuid %s'.\n", state->command, state->command);
		fprintf(stderr, "Instead, it's possible that you messed up the disk mount points,\n");
		fprintf(stderr, "and you have to restore the mount points at the state of the latest sync.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* count the number of data disks, including holes left after removing some */
	diskcount = 0;
	for(i=state->maplist;i!=0;i=i->next) {
		struct snapraid_map* map = i->data;

		if (map->position + 1 > diskcount)
			diskcount = map->position + 1;
	}
	if (diskcount > RAID_DATA_MAX) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Too many data disks. No more than %u.\n", RAID_DATA_MAX);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* recommend number of parities */
	if (diskcount >= 36 && state->level < 6) {
		fprintf(stderr, "WARNING! With %u disks it's recommended to use six parity levels.\n", diskcount);
	} else if (diskcount >= 29 && state->level < 5) {
		fprintf(stderr, "WARNING! With %u disks it's recommended to use five parity levels.\n", diskcount);
	} else if (diskcount >= 22 && state->level < 4) {
		fprintf(stderr, "WARNING! With %u disks it's recommended to use four parity levels.\n", diskcount);
	} else if (diskcount >= 15 && state->level < 3) {
		fprintf(stderr, "WARNING! With %u disks it's recommended to use three parity levels.\n", diskcount);
	} else if (diskcount >= 5 && state->level < 2) {
		fprintf(stderr, "WARNING! With %u disks it's recommended to use two parity levels.\n", diskcount);
	}
}

/**
 * Checks the content.
 */
static void state_content_check(struct snapraid_state* state, const char* path)
{
	tommy_node* i;

	/* checks that any map has different name and position */
	for(i=state->maplist;i!=0;i=i->next) {
		struct snapraid_map* map = i->data;
		tommy_node* j;
		for(j=i->next;j!=0;j=j->next) {
			struct snapraid_map* other = j->data;
			if (strcmp(map->name, other->name) == 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Colliding 'map' disk specification in '%s'\n", path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (map->position == other->position) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Colliding 'map' index specification in '%s'\n", path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}
}

/**
 * Checks if the position is REQUIRED, or we can completely clear it from the state.
 *
 * Note that position with only DELETED blocks are discarged.
 */
static int position_is_required(struct snapraid_state* state, block_off_t pos)
{
	tommy_node* i;

	/* check for each disk */
	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_block* block = disk_block_get(disk, pos);

		/* if we have at least one file, the position is needed */
		if (block_has_file(block))
			return 1;
	}

	return 0;
}

/**
 * Checks if the info block is REQUIREQ.
 *
 * This is used to ensure that we keep the last check used for scrubbing.
 * and the we add it when importing old context files.
 *
 * Note that you can have position without info blocks, for example
 * if all the blocks are not synced.
 *
 * Note also that not requiring an info block, doesn't mean that if present it
 * can be discarded.
 */
static int info_is_required(struct snapraid_state* state, block_off_t pos)
{
	tommy_node* i;

	/* check for each disk */
	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_block* block = disk_block_get(disk, pos);

		/* if we have at least one synced file, the info is required */
		if (block_state_get(block) == BLOCK_STATE_BLK)
			return 1;
	}

	return 0;
}

static void position_clear_deleted(struct snapraid_state* state, block_off_t pos)
{
	tommy_node* i;

	/* check for each disk if block is really used */
	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_block* block = disk_block_get(disk, pos);

		/* if the block is deleted */
		if (block_state_get(block) == BLOCK_STATE_DELETED) {
			/* set it to empty */
			tommy_arrayblk_set(&disk->blockarr, pos, BLOCK_EMPTY);
		}
	}
}

/**
 * Checks if a block position in a disk is deleted.
 */
static int is_block_deleted(struct snapraid_disk* disk, block_off_t pos)
{
	struct snapraid_block* block = disk_block_get(disk, pos);

	return block_state_get(block) == BLOCK_STATE_DELETED;
}

/**
 * Finds a disk by name.
 */
static struct snapraid_disk* find_disk(struct snapraid_state* state, const char* name)
{
	tommy_node* i;

	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_disk* disk = i->data;
		if (strcmp(disk->name, name) == 0)
			return disk;
	}

	if (state->no_conf) {
		/* without a configuration file, add disks automatically */
		struct snapraid_disk* disk;

		disk = disk_alloc(name, "DUMMY/", -1);

		tommy_list_insert_tail(&state->disklist, &disk->node, disk);

		return disk;
	}

	return 0;
}

static void state_read_text(struct snapraid_state* state, const char* path, STREAM* f, time_t save_time)
{
	struct snapraid_disk* disk;
	struct snapraid_file* file;
	block_off_t blockidx;
	block_off_t blockmax;
	block_off_t paritymax;
	unsigned line;
	unsigned count_file;
	unsigned count_block;
	unsigned count_hardlink;
	unsigned count_symlink;
	unsigned count_dir;
	block_off_t b;

	disk = 0;
	file = 0;
	line = 1;
	blockidx = 0;
	blockmax = 0;
	paritymax = 0;
	count_file = 0;
	count_block = 0;
	count_hardlink = 0;
	count_symlink = 0;
	count_dir = 0;

	while (1) {
		char buffer[PATH_MAX];
		char tag[PATH_MAX];
		int ret;
		int c;

		/* read the command */
		ret = sgettok(f, tag, sizeof(tag));
		if (ret < 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error reading the configuration file '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* skip only one space if present */
		c = sgetc(f);
		if (c != ' ') {
			sungetc(c, f);
		}

		if (strcmp(tag, "blk") == 0 || strcmp(tag, "new") == 0
			|| strcmp(tag, "chg") == 0 || strcmp(tag, "rep") == 0
		) {
			/* "blk"/""new"/"chg"/"rep" command */
			block_off_t v_pos;
			struct snapraid_block* block;

			if (!file) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Unexpected '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetu32(f, &v_pos);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (blockidx >= file->blockmax) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Internal inconsistency in '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			block = &file->blockvec[blockidx];

			if (block->parity_pos != POS_INVALID) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Internal inconsistency in '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* set the parity only if present */
			switch (tag[0]) {
			case 'b' :
				block_state_set(block, BLOCK_STATE_BLK);

				/* keep track of the required parity size */
				if (v_pos + 1 > paritymax)
					paritymax = v_pos + 1;
				break;
			case 'n' :
				/* deprecated NEW blocks are converted to CHG ones */
				block_state_set(block, BLOCK_STATE_CHG);
				break;
			case 'c' :
				block_state_set(block, BLOCK_STATE_CHG);
				break;
			case 'p' :
				block_state_set(block, BLOCK_STATE_REP);
				break;
			}

			block->parity_pos = v_pos;

			/* keep track of the max block number used in parity */
			if (v_pos + 1 > blockmax)
				blockmax = v_pos + 1;

			/* read the hash only for 'blk/chg/rep', and not for 'new' */
			if (tag[0] != 'n') {
				c = sgetc(f);
				if (c != ' ') {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Invalid '%s' specification in '%s' at line %u\n", tag, path, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				ret = sgethex(f, block->hash, HASH_SIZE);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Invalid '%s' specification in '%s' at line %u\n", tag, path, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			} else {
				/* set the ZERO hash for deprecated NEW blocks */
				hash_zero_set(block->hash);
			}

			/* if the block contains a hash of past data */
			/* and we are clearing such undeterminated hashes */
			if (state->clear_undeterminate_hash
				&& block_has_past_hash(block)
			) {
				/* set the hash value to INVALID */
				hash_invalid_set(block->hash);
			}

			/* we must not overwrite existing blocks */
			if (disk_block_get(disk, v_pos) != BLOCK_EMPTY) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Internal inconsistency for '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* insert the block in the block array */
			tommy_arrayblk_grow(&disk->blockarr, v_pos + 1);
			tommy_arrayblk_set(&disk->blockarr, v_pos, block);

			/* check for termination of the block list */
			++blockidx;
			if (blockidx == file->blockmax) {
				file = 0;
				disk = 0;
			}

			/* stat */
			++count_block;
		} else if (strcmp(tag, "inf") == 0) {
			/* "inf" command */
			block_off_t v_pos;
			snapraid_info info;
			int rehash;
			int bad;
			uint32_t t;

			ret = sgetu32(f, &v_pos);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'inf' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (v_pos >= blockmax) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Internal position inconsistency in 'inf' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'inf' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetu32(f, &t);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'inf' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* read extra tags if present */
			rehash = 0;
			bad = 0;
			c = sgetc(f);
			while (c == ' ') {
				ret = sgettok(f, buffer, sizeof(buffer));
				if (ret < 0) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Invalid 'inf' specification in '%s' at line %u\n", path, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				if (strcmp(buffer, "bad") == 0) {
					bad = 1;
				} else if (strcmp(buffer, "rehash") == 0) {
					rehash = 1;

					if (state->prevhash == HASH_UNDEFINED) {
						/* LCOV_EXCL_START */
						fprintf(stderr, "Internal inconsistency for missing previous checksum in '%s' at line %u\n", path, line);
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}
				} else {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Invalid 'inf' specification '%s' in '%s' at line %u\n", buffer, path, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				c = sgetc(f);
			}
			sungetc(c, f);

			info = info_make(t, bad, rehash);

			/* insert the info in the array */
			info_set(&state->infoarr, v_pos, info);
		} else if (strcmp(tag, "off") == 0) {
			/* "off" command */
			block_off_t v_pos;
			struct snapraid_deleted* deleted;

			if (!disk) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Unexpected 'off' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* allocate a new deleted block */
			deleted = deleted_alloc();

			/* insert it in the list of deleted blocks */
			tommy_list_insert_tail(&disk->deletedlist, &deleted->node, deleted);

			ret = sgetu32(f, &v_pos);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'off' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			deleted->block.parity_pos = v_pos;

			/* read the hash */
			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'off' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* set the hash only if present */
			ret = sgethex(f, deleted->block.hash, HASH_SIZE);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'off' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* if we are clearing undeterminated hashes */
			if (state->clear_undeterminate_hash) {
				/* set the hash value to INVALID */
				hash_invalid_set(deleted->block.hash);
			}

			/* we must not overwrite existing blocks */
			if (disk_block_get(disk, v_pos) != BLOCK_EMPTY) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Internal inconsistency for 'off' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* insert the block in the block array */
			tommy_arrayblk_grow(&disk->blockarr, v_pos + 1);
			tommy_arrayblk_set(&disk->blockarr, v_pos, &deleted->block);
		} else if (strcmp(tag, "file") == 0) {
			/* file */
			char sub[PATH_MAX];
			uint64_t v_size;
			uint64_t v_mtime_sec;
			uint32_t v_mtime_nsec;
			uint64_t v_inode;

			if (file) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Missing 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetu64(f, &v_size);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetu64(f, &v_mtime_sec);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c == '.') { /* the nanosecond field is present only from version 1.14 */
				ret = sgetu32(f, &v_mtime_nsec);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				c = sgetc(f);
			} else {
				/* use a special value, meaning that we don't have this information */
				v_mtime_nsec = STAT_NSEC_INVALID;
			}

			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetu64(f, &v_inode);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetline(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*sub) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* find the disk */
			disk = find_disk(state, buffer);
			if (!disk) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Disk named '%s' not found in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* allocate the file */
			file = file_alloc(state->block_size, sub, v_size, v_mtime_sec, v_mtime_nsec, v_inode, 0);

			/* insert the file in the file containers */
			tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));
			tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));
			tommy_hashdyn_insert(&disk->stampset, &file->stampset, file, file_stamp_hash(file->size, file->mtime_sec, file->mtime_nsec));
			tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);

			/* start the block allocation of the file */
			blockidx = 0;

			/* check for empty file */
			if (blockidx == file->blockmax) {
				file = 0;
				disk = 0;
			}

			/* stat */
			++count_file;
		} else if (strcmp(tag, "hole") == 0) {
			/* hole */

			if (file) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Missing 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hole' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* find the disk */
			disk = find_disk(state, buffer);
			if (!disk) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Disk named '%s' not found in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else if (strcmp(tag, "symlink") == 0) {
			/* symlink */
			char sub[PATH_MAX];
			char linkto[PATH_MAX];
			char tokento[32];
			struct snapraid_link* link;

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetline(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgeteol(f);
			if (c != '\n') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			++line;

			ret = sgettok(f, tokento, sizeof(tokento));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (strcmp(tokento, "to") != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetline(f, linkto, sizeof(linkto));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*sub || !*linkto) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* find the disk */
			disk = find_disk(state, buffer);
			if (!disk) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Disk named '%s' not found in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* allocate the link as symbolic link */
			link = link_alloc(sub, linkto, FILE_IS_SYMLINK);

			/* insert the link in the link containers */
			tommy_hashdyn_insert(&disk->linkset, &link->nodeset, link, link_name_hash(link->sub));
			tommy_list_insert_tail(&disk->linklist, &link->nodelist, link);

			/* stat */
			++count_symlink;
		} else if (strcmp(tag, "hardlink") == 0) {
			/* hardlink */
			char sub[PATH_MAX];
			char linkto[PATH_MAX];
			char tokento[32];
			struct snapraid_link* link;

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hardlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hardlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetline(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hardlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgeteol(f);
			if (c != '\n') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hardlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			++line;

			ret = sgettok(f, tokento, sizeof(tokento));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hardlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (strcmp(tokento, "to") != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hardlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hardlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetline(f, linkto, sizeof(linkto));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hardlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*sub || !*linkto) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'hardlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* find the disk */
			disk = find_disk(state, buffer);
			if (!disk) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Disk named '%s' not found in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* allocate the link as hard link */
			link = link_alloc(sub, linkto, FILE_IS_HARDLINK);

			/* insert the link in the link containers */
			tommy_hashdyn_insert(&disk->linkset, &link->nodeset, link, link_name_hash(link->sub));
			tommy_list_insert_tail(&disk->linklist, &link->nodelist, link);

			/* stat */
			++count_hardlink;
		} else if (strcmp(tag, "dir") == 0) {
			/* dir */
			char sub[PATH_MAX];
			struct snapraid_dir* dir;

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'dir' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'dir' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetline(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'dir' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*sub) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'dir' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* find the disk */
			disk = find_disk(state, buffer);
			if (!disk) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Disk named '%s' not found in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* allocate the dir */
			dir = dir_alloc(sub);

			/* insert the dir in the dir containers */
			tommy_hashdyn_insert(&disk->dirset, &dir->nodeset, dir, dir_name_hash(dir->sub));
			tommy_list_insert_tail(&disk->dirlist, &dir->nodelist, dir);

			/* stat */
			++count_dir;
		} else if (strcmp(tag, "checksum") == 0) {

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'checksum' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (strcmp(buffer, "murmur3") == 0) {
				state->hash = HASH_MURMUR3;
			} else if (strcmp(buffer, "spooky2") == 0) {
				state->hash = HASH_SPOOKY2;
			} else {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'checksum' specification '%s' in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c == ' ') {
				/* read the seed if present */
				ret = sgethex(f, state->hashseed, HASH_SIZE);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Invalid 'seed' specification in '%s' at line %u\n", path, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			} else {
				sungetc(c, f);
				memset(state->hashseed, 0, HASH_SIZE);
			}
		} else if (strcmp(tag, "prevchecksum") == 0) {

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'prevchecksum' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (strcmp(buffer, "murmur3") == 0) {
				state->prevhash = HASH_MURMUR3;
			} else if (strcmp(buffer, "spooky2") == 0) {
				state->prevhash = HASH_SPOOKY2;
			} else {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'prevchecksum' specification '%s' in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'prevchecksum' specification '%s' in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* read the seed */
			ret = sgethex(f, state->prevhashseed, HASH_SIZE);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'seed' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else if (strcmp(tag, "blksize") == 0) {
			block_off_t blksize;

			ret = sgetu32(f, &blksize);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'blksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* without configuration, auto assign the block size */
			if (state->no_conf) {
				state->block_size = blksize;
			}

			if (blksize != state->block_size) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Mismatching 'blksize' and 'blocksize' specification in '%s' at line %u\n", path, line);
				fprintf(stderr, "Please restore the 'blocksize' value in the configuration file to '%u'\n", blksize / 1024);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else if (strcmp(tag, "map") == 0) {
			struct snapraid_map* map;
			char uuid[UUID_MAX];
			uint32_t v_pos;

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'map' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'map' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetu32(f, &v_pos);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'map' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			c = sgetc(f);
			if (c != ' ') {
				sungetc(c, f);
				uuid[0] = 0;
			} else {
				/* read the uuid */
				ret = sgettok(f, uuid, sizeof(uuid));
				if (ret < 0) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Invalid 'map' specification in '%s' at line %u\n", path, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}

			map = map_alloc(buffer, v_pos, uuid);

			tommy_list_insert_tail(&state->maplist, &map->node, map);
		} else if (strcmp(tag, "sign") == 0) {
			uint32_t sign;

			ret = sgetu32(f, &sign);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid 'sign' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* sign check not supported anymore for the text content file */
		} else if (tag[0] == 0) {
			/* allow empty lines */
			sgetspace(f);
		} else if (tag[0] == '#') {
			ret = sgetline(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Invalid comment in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Invalid command '%s' in '%s' at line %u\n", tag, path, line);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* next line */
		c = sgeteol(f);
		if (c == EOF) {
			break;
		}
		if (c != '\n') {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Extra data in '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		++line;
	}

	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error reading the content file '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (file) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Missing 'blk' specification in '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* check that the stored parity size matches the loaded state */
	if (blockmax != parity_size(state)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Internal inconsistency in parity size in '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* to be backward compatible with old context files without info */
	/* create info records if missing */
	for(b=0;b<blockmax;++b) {
		/* if the position is used */
		if (info_is_required(state, b)) {
			snapraid_info info = info_get(&state->infoarr, b);

			/* if no info is present  */
			if (!info) {
				/* set a fake info block */
				info = info_make(save_time, 0, 0);

				/* insert the info in the array */
				info_set(&state->infoarr, b, info);
			}
		}
	}

	/* set the required parity size */
	state->loaded_paritymax = paritymax;

	if (state->opt.verbose) {
		printf("%8u files\n", count_file);
		printf("%8u blocks\n", count_block);
		printf("%8u hardlinks\n", count_hardlink);
		printf("%8u symlinks\n", count_symlink);
		printf("%8u empty dirs\n", count_dir);
	}
}

static void state_write_text(struct snapraid_state* state, STREAM* f)
{
	unsigned count_file;
	unsigned count_block;
	unsigned count_hardlink;
	unsigned count_symlink;
	unsigned count_dir;
	tommy_node* i;
	block_off_t b;
	block_off_t blockmax;
	int info_has_rehash;

	count_file = 0;
	count_block = 0;
	count_hardlink = 0;
	count_symlink = 0;
	count_dir = 0;

	/* blocks of all array */
	blockmax = parity_size(state);

	/* clear the info for unused blocks */
	/* and get some other info */
	info_has_rehash = 0; /* if there is a rehash info */
	for(b=0;b<blockmax;++b) {
		/* if the position is used */
		if (position_is_required(state, b)) {
			snapraid_info info = info_get(&state->infoarr, b);

			/* only if there is some info to store */
			if (info) {
				if (info_get_rehash(info))
					info_has_rehash = 1;
			}
		} else {
			/* clear any previous info */
			info_set(&state->infoarr, b, 0);

			/* and clear any deleted blocks */
			position_clear_deleted(state, b);
		}
	}

	sputsl("blksize ", f);
	sputu32(state->block_size, f);
	sputeol(f);
	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (state->hash == HASH_MURMUR3) {
		sputsl("checksum murmur3", f);
	} else if (state->hash == HASH_SPOOKY2) {
		sputsl("checksum spooky2", f);
	} else {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Unexpected hash when writing the content file '%s'.\n", serrorfile(f));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	sputc(' ', f);
	sputhex(state->hashseed, HASH_SIZE, f);
	sputeol(f);
	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* previous hash only present */
	if (state->prevhash != HASH_UNDEFINED) {
		/* if at least one rehash tag found, we have to save the previous hash */
		if (info_has_rehash) {
			if (state->prevhash == HASH_MURMUR3) {
				sputsl("prevchecksum murmur3", f);
			} else if (state->prevhash == HASH_SPOOKY2) {
				sputsl("prevchecksum spooky2", f);
			} else {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Unexpected prevhash when writing the content file '%s'.\n", serrorfile(f));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			sputc(' ', f);
			sputhex(state->prevhashseed, HASH_SIZE, f);
			sputeol(f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/* for each map */
	for(i=state->maplist;i!=0;i=i->next) {
		struct snapraid_map* map = i->data;
		struct snapraid_disk* disk;

		/* find the disk for this mapping */
		disk = find_disk(state, map->name);
		if (!disk) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Internal inconsistency for unmapped disk '%s'\n", map->name);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* save the mapping only for not empty disks */
		if (!disk_is_empty(disk, blockmax)) {
			sputsl("map ", f);
			sputs(map->name, f);
			sputc(' ', f);
			sputu32(map->position, f);

			/* if there is an uuid, print it */
			if (map->uuid[0]) {
				sputc(' ', f);
				sputs(map->uuid, f);
			}
			sputeol(f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* mark the disk as with mapping */
			disk->mapping = 0;
		} else {
			/* mark the disk as without mapping */
			disk->mapping = -1;
		}
	}

	/* for each disk */
	for(i=state->disklist;i!=0;i=i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* if the disk is not mapped, skip it */
		if (disk->mapping < 0)
			continue;

		/* for each file */
		for(j=disk->filelist;j!=0;j=j->next) {
			struct snapraid_file* file = j->data;
			uint64_t size;
			uint64_t mtime_sec;
			int32_t mtime_nsec;
			uint64_t inode;

			size = file->size;
			mtime_sec = file->mtime_sec;
			mtime_nsec = file->mtime_nsec;
			inode = file->inode;

			sputsl("file ", f);
			sputs(disk->name, f);
			sputc(' ', f);
			sputu64(size, f);
			sputc(' ', f);
			sputu64(mtime_sec, f);
			if (mtime_nsec != STAT_NSEC_INVALID) {
				sputc('.', f);
				sputu32(mtime_nsec, f);
			}
			sputc(' ', f);
			sputu64(inode, f);
			sputc(' ', f);
			sputs(file->sub, f);
			sputeol(f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* for each block in the file */
			for(b=0;b<file->blockmax;++b) {
				struct snapraid_block* block = &file->blockvec[b];
				unsigned block_state;

				block_state = block_state_get(block);
				switch (block_state) {
				case BLOCK_STATE_BLK :
					sputsl("blk ", f);
					break;
				case BLOCK_STATE_CHG :
					sputsl("chg ", f);
					break;
				case BLOCK_STATE_REP :
					sputsl("rep ", f);
					break;
				default:
					/* LCOV_EXCL_START */
					fprintf(stderr, "Internal state inconsistency in saving for block %u state %u\n", block->parity_pos, block_state);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				sputu32(block->parity_pos, f);

				sputc(' ', f);
				sputhex(block->hash, HASH_SIZE, f);

				sputeol(f);
				if (serror(f)) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				++count_block;
			}

			++count_file;
		}

		/* for each link */
		for(j=disk->linklist;j!=0;j=j->next) {
			struct snapraid_link* link = j->data;

			switch (link_flag_get(link, FILE_IS_LINK_MASK)) {
			case FILE_IS_HARDLINK :
				sputsl("hardlink ", f);
				++count_hardlink;
				break;
			case FILE_IS_SYMLINK :
				sputsl("symlink ", f);
				++count_symlink;
				break;
			}

			sputs(disk->name, f);
			sputc(' ', f);
			sputs(link->sub, f);
			sputeol(f);
			sputsl("to ", f);
			sputs(link->linkto, f);
			sputeol(f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		/* for each dir */
		for(j=disk->dirlist;j!=0;j=j->next) {
			struct snapraid_dir* dir = j->data;

			sputsl("dir ", f);
			sputs(disk->name, f);
			sputc(' ', f);
			sputs(dir->sub, f);
			sputeol(f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			++count_dir;
		}

		/* deleted blocks of the disk */
		sputsl("hole ", f);
		sputs(disk->name, f);
		sputeol(f);
		if (serror(f)) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		for(b=0;b<blockmax;++b) {
			if (is_block_deleted(disk, b)) {
				struct snapraid_block* block = disk_block_get(disk, b);
			
				sputsl("off ", f);
				sputu32(b, f);
				sputc(' ', f);
				sputhex(block->hash, HASH_SIZE, f);
				sputeol(f);
				if (serror(f)) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
		}
	}

	/* write the info for each block */
	for(b=0;b<blockmax;++b) {
		snapraid_info info = info_get(&state->infoarr, b);

		/* save only stuffs different than 0 */
		if (info != 0) {
			sputsl("inf ", f);

			sputu32(b, f);
			sputc(' ', f);
			sputu32(info, f);
			if (info_get_bad(info)) {
				sputsl(" bad", f);
			}
			if (info_get_rehash(info)) {
				sputsl(" rehash", f);
			}
			sputeol(f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}

	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (state->opt.verbose) {
		printf("%8u files\n", count_file);
		printf("%8u blocks\n", count_block);
		printf("%8u hardlinks\n", count_hardlink);
		printf("%8u symlinks\n", count_symlink);
		printf("%8u empty dirs\n", count_dir);
	}
}

/**
 * Flush the file checking the final CRC.
 * We exploit the fact that the CRC is always stored in the last 4 bytes.
 */
static void decoding_error(const char* path, STREAM* f)
{
	unsigned char buf[4];
	uint32_t crc_stored;
	uint32_t crc_computed;

	if (seof(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Unexpected end of content file '%s' at offset %"PRIi64"\n", path, stell(f));
		fprintf(stderr, "This content file is truncated. Use an alternate copy.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error reading the content file '%s' at offset %"PRIi64"\n", path, stell(f));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	fprintf(stderr, "Decoding error in '%s' at offset %"PRIi64"\n", path, stell(f));

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0;

	/* read until the end of the file */
	while (1) {
		int c = sgetc(f);
		if (c == EOF) {
			break;
		}

		/* keep the last four bytes */
		buf[0] = buf[1];
		buf[1] = buf[2];
		buf[2] = buf[3];
		buf[3] = c;
	}

	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error flushing the content file '%s' at offset %"PRIi64"\n", path, stell(f));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* get the stored crc from the last four bytes */
	crc_stored = buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;

	/* get the computed crc */
	crc_computed = scrc(f);

	/* adjust the stored crc to include itself */
	crc_stored = crc32c(crc_stored, buf, 4);

	if (crc_computed != crc_stored) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Mismatching CRC in '%s'\n", path);
		fprintf(stderr, "This content file is damaged! Use an alternate copy.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	} else {
		fprintf(stderr, "The file CRC is correct!\n");
	}
}

static void state_read_binary(struct snapraid_state* state, const char* path, STREAM* f)
{
	block_off_t blockmax;
	block_off_t paritymax;
	unsigned count_file;
	unsigned count_block;
	unsigned count_hardlink;
	unsigned count_symlink;
	unsigned count_dir;
	int crc_checked;
	char buffer[PATH_MAX];
	int ret;
	tommy_array disk_mapping;
	uint32_t mapping_max;

	blockmax = 0;
	paritymax = 0;
	count_file = 0;
	count_block = 0;
	count_hardlink = 0;
	count_symlink = 0;
	count_dir = 0;
	crc_checked = 0;
	mapping_max = 0;
	tommy_array_init(&disk_mapping);

	ret = sread(f, buffer, 12);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		decoding_error(path, f);
		fprintf(stderr, "Invalid header!\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (memcmp(buffer, "SNAPCNT1\n\3\0\0", 12) != 0) {
		/* LCOV_EXCL_START */
		if (memcmp(buffer, "SNAPCNT", 7) != 0) {
			decoding_error(path, f);
			fprintf(stderr, "Invalid header!\n");
		} else {
			fprintf(stderr, "The content file '%s' was generated with a newer version of SnapRAID!\n", path);
		}
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	while (1) {
		char buffer[PATH_MAX];
		int c;

		/* read the command */
		c = sgetc(f);
		if (c == EOF) {
			break;
		}

		if (c == 'f') {
			/* file */
			char sub[PATH_MAX];
			uint64_t v_size;
			uint64_t v_mtime_sec;
			uint32_t v_mtime_nsec;
			uint64_t v_inode;
			uint32_t v_idx;
			struct snapraid_file* file;
			struct snapraid_disk* disk;
			uint32_t mapping;

			ret = sgetb32(f, &mapping);
			if (ret < 0 || mapping >= mapping_max) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Internal inconsistency in mapping index!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			ret = sgetb64(f, &v_size);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* check for impossible file size to avoid to crash for a too big allocation */
			if (v_size / state->block_size > blockmax) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Internal inconsistency in file size too big!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb64(f, &v_mtime_sec);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb32(f, &v_mtime_nsec);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* STAT_NSEC_INVALID is encoded as 0 */
			if (v_mtime_nsec == 0)
				v_mtime_nsec = STAT_NSEC_INVALID;
			else
				--v_mtime_nsec;

			ret = sgetb64(f, &v_inode);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetbs(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (!*sub) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* allocate the file */
			file = file_alloc(state->block_size, sub, v_size, v_mtime_sec, v_mtime_nsec, v_inode, 0);

			/* insert the file in the file containers */
			tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));
			tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));
			tommy_hashdyn_insert(&disk->stampset, &file->stampset, file, file_stamp_hash(file->size, file->mtime_sec, file->mtime_nsec));
			tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);

			/* reads all the blocks */
			v_idx = 0;
			while (v_idx < file->blockmax) {
				block_off_t v_pos;
				uint32_t v_count;

				/* get the "subcommand */
				c = sgetc(f);

				ret = sgetb32(f, &v_pos);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				ret = sgetb32(f, &v_count);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				if (v_idx + v_count > file->blockmax) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					fprintf(stderr, "Internal inconsistency in block number!\n");
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				if (v_pos + v_count > blockmax) {
					/* LCOV_EXCL_START */
					if (state->opt.skip_content_check) {
						fprintf(stderr, "Internal inconsistency in block size!\n");
						fprintf(stderr, "Overriding from %u to %u...\n", blockmax, v_pos + v_count);
						blockmax = v_pos + v_count;
					} else {
						fprintf(stderr, "Internal inconsistency in block size!\n");
						decoding_error(path, f);
						exit(EXIT_FAILURE);
					}
					/* LCOV_EXCL_START */
				}

				/* grow the array */
				tommy_arrayblk_grow(&disk->blockarr, v_pos + v_count);

				/* fill the blocks in the run */
				while (v_count) {
					struct snapraid_block* block = &file->blockvec[v_idx];

					if (block->parity_pos != POS_INVALID) {
						/* LCOV_EXCL_START */
						decoding_error(path, f);
						fprintf(stderr, "Internal inconsistency in block position!\n");
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}

					switch (c) {
					case 'b' :
						block_state_set(block, BLOCK_STATE_BLK);

						/* keep track of the required parity size */
						if (v_pos + 1 > paritymax)
							paritymax = v_pos + 1;
						break;
					case 'n' :
						/* deprecated NEW blocks are converted to CHG ones */
						block_state_set(block, BLOCK_STATE_CHG);
						break;
					case 'g' :
						block_state_set(block, BLOCK_STATE_CHG);
						break;
					case 'p' :
						block_state_set(block, BLOCK_STATE_REP);
						break;
					default:
						/* LCOV_EXCL_START */
						decoding_error(path, f);
						fprintf(stderr, "Invalid block type!\n");
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}

					block->parity_pos = v_pos;

					/* read the hash only for 'blk/chg/rep', and not for 'new' */
					if (c != 'n') {
						ret = sread(f, block->hash, HASH_SIZE);
						if (ret < 0) {
							/* LCOV_EXCL_START */
							decoding_error(path, f);
							exit(EXIT_FAILURE);
							/* LCOV_EXCL_STOP */
						}
					} else {
						/* set the ZERO hash for deprecated NEW blocks */
						hash_zero_set(block->hash);
					}

					/* if the block contains a hash of past data */
					/* and we are clearing such undeterminated hashes */
					if (state->clear_undeterminate_hash
						&& block_has_past_hash(block)
					) {
						/* set the hash value to INVALID */
						hash_invalid_set(block->hash);
					}

					/* we must not overwrite existing blocks */
					if (disk_block_get(disk, v_pos) != BLOCK_EMPTY) {
						/* LCOV_EXCL_START */
						decoding_error(path, f);
						fprintf(stderr, "Internal inconsistency in block existence!\n");
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}

					/* insert the block in the block array */
					tommy_arrayblk_set(&disk->blockarr, v_pos, block);

					/* stat */
					++count_block;

					/* go to the next block */
					++v_idx;
					++v_pos;
					--v_count;
				}
			}

			/* stat */
			++count_file;
		} else if (c == 'i') {
			/* "inf" command */
			snapraid_info info;
			uint32_t v_pos;
			uint32_t v_oldest;

			ret = sgetb32(f, &v_oldest);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			v_pos = 0;
			while (v_pos < blockmax) {
				int rehash;
				int bad;
				uint32_t t;
				uint32_t flag;
				uint32_t v_count;

				ret = sgetb32(f, &v_count);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				if (v_pos + v_count > blockmax) {
					/* LCOV_EXCL_START */
					if (state->opt.skip_content_check) {
						fprintf(stderr, "Internal inconsistency in info size!\n");
						fprintf(stderr, "Overriding from %u to %u...\n", blockmax, v_pos + v_count);
						blockmax = v_pos + v_count;
					} else {
						decoding_error(path, f);
						fprintf(stderr, "Internal inconsistency in info size!\n");
						exit(EXIT_FAILURE);
					}
					/* LCOV_EXCL_STOP */
				}

				ret = sgetb32(f, &flag);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				/* if there is an info */
				if ((flag & 1) != 0) {
					/* read the time */
					ret = sgetb32(f, &t);
					if (ret < 0) {
						/* LCOV_EXCL_START */
						decoding_error(path, f);
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}

					/* analyze the flags */
					bad = (flag & 2) != 0;
					rehash = (flag & 4) != 0;

					if (rehash && state->prevhash == HASH_UNDEFINED) {
						/* LCOV_EXCL_START */
						decoding_error(path, f);
						fprintf(stderr, "Internal inconsistency for missing previous checksum!\n");
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}

					info = info_make(t + v_oldest, bad, rehash);
				} else {
					info = 0;
				}

				while (v_count) {
					/* insert the info in the array */
					info_set(&state->infoarr, v_pos, info);

					/* ensure that an info is present only for used positions */
					if (info_is_required(state, v_pos)) {
						if (!info) {
							/* LCOV_EXCL_START */
							decoding_error(path, f);
							fprintf(stderr, "Internal inconsistency for missing info!\n");
							exit(EXIT_FAILURE);
							/* LCOV_EXCL_STOP */
						}
					} else {
						/* extra info are accepted for backward compatibility */
						/* they are discarged at the first write */
					}

					/* go to next block */
					++v_pos;
					--v_count;
				}
			}
		} else if (c == 'h') {
			/* hole */
			uint32_t v_pos;
			struct snapraid_disk* disk;
			uint32_t mapping;

			ret = sgetb32(f, &mapping);
			if (ret < 0 || mapping >= mapping_max) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Internal inconsistency in mapping index!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			v_pos = 0;
			while (v_pos < blockmax) {
				uint32_t v_count;

				ret = sgetb32(f, &v_count);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				if (v_pos + v_count > blockmax) {
					/* LCOV_EXCL_START */
					if (state->opt.skip_content_check) {
						fprintf(stderr, "Internal inconsistency in hole size!\n");
						fprintf(stderr, "Overriding from %u to %u...\n", blockmax, v_pos + v_count);
						blockmax = v_pos + v_count;
					} else {
						decoding_error(path, f);
						fprintf(stderr, "Internal inconsistency in hole size!\n");
						exit(EXIT_FAILURE);
					}
					/* LCOV_EXCL_STOP */
				}

				/* get the subcommand */
				c = sgetc(f);

				switch (c) {
				case 'o' :
					/* grow the array */
					tommy_arrayblk_grow(&disk->blockarr, v_pos + v_count);

					/* if it's a run of deleted blocks */
					while (v_count) {
						struct snapraid_deleted* deleted;

						/* allocate a new deleted block */
						deleted = deleted_alloc();

						/* insert it in the list of deleted blocks */
						tommy_list_insert_tail(&disk->deletedlist, &deleted->node, deleted);

						/* set the position */
						deleted->block.parity_pos = v_pos;

						/* read the hash */
						ret = sread(f, deleted->block.hash, HASH_SIZE);
						if (ret < 0) {
							/* LCOV_EXCL_START */
							decoding_error(path, f);
							exit(EXIT_FAILURE);
							/* LCOV_EXCL_STOP */
						}

						/* if we are clearing undeterminated hashes */
						if (state->clear_undeterminate_hash) {
							/* set the hash value to INVALID */
							hash_invalid_set(deleted->block.hash);
						}

						/* we must not overwrite existing blocks */
						if (disk_block_get(disk, v_pos) != BLOCK_EMPTY) {
							/* LCOV_EXCL_START */
							decoding_error(path, f);
							fprintf(stderr, "Internal inconsistency for used hole!\n");
							exit(EXIT_FAILURE);
							/* LCOV_EXCL_STOP */
						}

						/* insert the block in the block array */
						tommy_arrayblk_set(&disk->blockarr, v_pos, &deleted->block);

						/* go to next block */
						++v_pos;
						--v_count;
					}
					break;
				case 'O' :
					/* go to the next run */
					v_pos += v_count;
					break;
				default:
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					fprintf(stderr, "Invalid hole type!\n");
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
		} else if (c == 's') {
			/* symlink */
			char sub[PATH_MAX];
			char linkto[PATH_MAX];
			struct snapraid_link* link;
			struct snapraid_disk* disk;
			uint32_t mapping;

			ret = sgetb32(f, &mapping);
			if (ret < 0 || mapping >= mapping_max) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Internal inconsistency in mapping index!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			ret = sgetbs(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetbs(f, linkto, sizeof(linkto));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*sub || !*linkto) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* allocate the link as symbolic link */
			link = link_alloc(sub, linkto, FILE_IS_SYMLINK);

			/* insert the link in the link containers */
			tommy_hashdyn_insert(&disk->linkset, &link->nodeset, link, link_name_hash(link->sub));
			tommy_list_insert_tail(&disk->linklist, &link->nodelist, link);

			/* stat */
			++count_symlink;
		} else if (c == 'a') {
			/* hardlink */
			char sub[PATH_MAX];
			char linkto[PATH_MAX];
			struct snapraid_link* link;
			struct snapraid_disk* disk;
			uint32_t mapping;

			ret = sgetb32(f, &mapping);
			if (ret < 0 || mapping >= mapping_max) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Internal inconsistency in mapping index!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			ret = sgetbs(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetbs(f, linkto, sizeof(linkto));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*sub || !*linkto) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* allocate the link as hard link */
			link = link_alloc(sub, linkto, FILE_IS_HARDLINK);

			/* insert the link in the link containers */
			tommy_hashdyn_insert(&disk->linkset, &link->nodeset, link, link_name_hash(link->sub));
			tommy_list_insert_tail(&disk->linklist, &link->nodelist, link);

			/* stat */
			++count_hardlink;
		} else if (c == 'r') {
			/* dir */
			char sub[PATH_MAX];
			struct snapraid_dir* dir;
			struct snapraid_disk* disk;
			uint32_t mapping;

			ret = sgetb32(f, &mapping);
			if (ret < 0 || mapping >= mapping_max) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Internal inconsistency in mapping index!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			ret = sgetbs(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*sub) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* allocate the dir */
			dir = dir_alloc(sub);

			/* insert the dir in the dir containers */
			tommy_hashdyn_insert(&disk->dirset, &dir->nodeset, dir, dir_name_hash(dir->sub));
			tommy_list_insert_tail(&disk->dirlist, &dir->nodelist, dir);

			/* stat */
			++count_dir;
		} else if (c == 'c') {
			/* get the subcommand */
			c = sgetc(f);

			switch (c) {
			case 'u' :
				state->hash = HASH_MURMUR3;
				break;
			case 'k' :
				state->hash = HASH_SPOOKY2;
				break;
			default:
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Invalid checksum!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* read the seed */
			ret = sread(f, state->hashseed, HASH_SIZE);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else if (c == 'C') {
			/* get the subcommand */
			c = sgetc(f);

			switch (c) {
			case 'u' :
				state->prevhash = HASH_MURMUR3;
				break;
			case 'k' :
				state->prevhash = HASH_SPOOKY2;
				break;
			default:
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Invalid checksum!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* read the seed */
			ret = sread(f, state->prevhashseed, HASH_SIZE);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else if (c == 'z') {
			block_off_t blksize;

			ret = sgetb32(f, &blksize);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* without configuration, auto assign the block size */
			if (state->no_conf) {
				state->block_size = blksize;
			}

			if (blksize != state->block_size) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Mismatching 'blksize' and 'blocksize' specification!\n");
				fprintf(stderr, "Please restore the 'blocksize' value in the configuration file to '%u'\n", blksize / 1024);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else if (c == 'x') {
			ret = sgetb32(f, &blockmax);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else if (c == 'm') {
			struct snapraid_map* map;
			char uuid[UUID_MAX];
			uint32_t v_pos;
			struct snapraid_disk* disk;

			ret = sgetbs(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb32(f, &v_pos);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* read the uuid */
			ret = sgetbs(f, uuid, sizeof(uuid));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			map = map_alloc(buffer, v_pos, uuid);

			tommy_list_insert_tail(&state->maplist, &map->node, map);

			/* find the disk */
			disk = find_disk(state, buffer);
			if (!disk) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				fprintf(stderr, "Disk named '%s' not present in the configuration file!\n", buffer);
				fprintf(stderr, "If you have removed it from the configuration file, please restore it\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* insert in the mapping vector */
			tommy_array_grow(&disk_mapping, mapping_max + 1);
			tommy_array_set(&disk_mapping, mapping_max, disk);
			++mapping_max;
		} else if (c == 'N') {
			uint32_t crc_stored;
			uint32_t crc_computed;

			/* get the crc before reading it from the file */
			crc_computed = scrc(f);

			ret = sgetble32(f, &crc_stored);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				/* here don't call decoding_error() because it's too late to get the crc */
				fprintf(stderr, "Error reading the CRC in '%s' at offset %"PRIi64"\n", path, stell(f));
				fprintf(stderr, "This content file is damaged! Use an alternate copy.\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (crc_stored != crc_computed) {
				/* LCOV_EXCL_START */
				/* here don't call decoding_error() because it's too late to get the crc */
				fprintf(stderr, "Mismatching CRC in '%s'\n", path);
				fprintf(stderr, "This content file is damaged! Use an alternate copy.\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			crc_checked = 1;
		} else {
			/* LCOV_EXCL_START */
			decoding_error(path, f);
			fprintf(stderr, "Invalid command '%c'!\n", (char)c);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	tommy_array_done(&disk_mapping);

	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error reading the content file '%s' at offset %"PRIi64"\n", path, stell(f));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (!crc_checked) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Finished reading '%s' without finding the CRC\n", path);
		fprintf(stderr, "This content file is truncated or damaged! Use an alternate copy.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* check that the stored parity size matches the loaded state */
	if (blockmax != parity_size(state)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Internal inconsistency in parity size in '%s' at offset %"PRIi64"\n", path, stell(f));
		if (state->opt.skip_content_check) {
			fprintf(stderr, "Overriding from %u to %u...\n", blockmax, parity_size(state));
			blockmax = parity_size(state);
		} else {
			exit(EXIT_FAILURE);
		}
		/* LCOV_EXCL_STOP */
	}

	/* set the required parity size */
	state->loaded_paritymax = paritymax;

	if (state->opt.verbose) {
		printf("%8u files\n", count_file);
		printf("%8u blocks\n", count_block);
		printf("%8u hardlinks\n", count_hardlink);
		printf("%8u symlinks\n", count_symlink);
		printf("%8u empty dirs\n", count_dir);
	}
}

static void state_write_binary(struct snapraid_state* state, STREAM* f)
{
	unsigned count_file;
	unsigned count_block;
	unsigned count_hardlink;
	unsigned count_symlink;
	unsigned count_dir;
	tommy_node* i;
	block_off_t b;
	block_off_t blockmax;
	block_off_t begin;
	time_t info_oldest;
	int info_has_rehash;
	int mapping_idx;

	count_file = 0;
	count_block = 0;
	count_hardlink = 0;
	count_symlink = 0;
	count_dir = 0;

	/* blocks of all array */
	blockmax = parity_size(state);

	/* clear the info for unused blocks */
	/* and get some other info */
	info_oldest = 0; /* oldest time in info */
	info_has_rehash = 0; /* if there is a rehash info */
	for(b=0;b<blockmax;++b) {
		/* if the position is used */
		if (position_is_required(state, b)) {
			snapraid_info info = info_get(&state->infoarr, b);

			/* only if there is some info to store */
			if (info) {
				time_t time = info_get_time(info);

				if (!info_oldest || time < info_oldest)
					info_oldest = time;

				if (info_get_rehash(info))
					info_has_rehash = 1;
			}
		} else {
			/* clear any previous info */
			info_set(&state->infoarr, b, 0);

			/* and clear any deleted blocks */
			position_clear_deleted(state, b);
		}
	}

	/* write header */
	swrite("SNAPCNT1\n\3\0\0", 12, f);

	/* write block size and block max */
	sputc('z', f);
	sputb32(state->block_size, f);
	sputc('x', f);
	sputb32(blockmax, f);
	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	sputc('c', f);
	if (state->hash == HASH_MURMUR3) {
		sputc('u', f);
	} else if (state->hash == HASH_SPOOKY2) {
		sputc('k', f);
	} else {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Unexpected hash when writing the content file '%s'.\n", serrorfile(f));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	swrite(state->hashseed, HASH_SIZE, f);
	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* previous hash only present */
	if (state->prevhash != HASH_UNDEFINED) {
		/* if at least one rehash tag found, we have to save the previous hash */
		if (info_has_rehash) {
			sputc('C', f);
			if (state->prevhash == HASH_MURMUR3) {
				sputc('u', f);
			} else if (state->prevhash == HASH_SPOOKY2) {
				sputc('k', f);
			} else {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Unexpected prevhash when writing the content file '%s'.\n", serrorfile(f));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			swrite(state->prevhashseed, HASH_SIZE, f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/* for each map */
	mapping_idx = 0;
	for(i=state->maplist;i!=0;i=i->next) {
		struct snapraid_map* map = i->data;
		struct snapraid_disk* disk;

		/* find the disk for this mapping */
		disk = find_disk(state, map->name);
		if (!disk) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Internal inconsistency for unmapped disk '%s'\n", map->name);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* save the mapping only for not empty disks */
		if (!disk_is_empty(disk, blockmax)) {
			sputc('m', f);
			sputbs(map->name, f);
			sputb32(map->position, f);
			sputbs(map->uuid, f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* assign the mapping index used to identify disks */
			disk->mapping = mapping_idx;
			++mapping_idx;
		} else {
			/* mark the disk as without mapping */
			disk->mapping = -1;
		}
	}

	/* for each disk */
	for(i=state->disklist;i!=0;i=i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* if the disk is not mapped, skip it */
		if (disk->mapping < 0)
			continue;

		/* for each file */
		for(j=disk->filelist;j!=0;j=j->next) {
			struct snapraid_file* file = j->data;
			struct snapraid_block* blockvec = file->blockvec;
			uint64_t size;
			uint64_t mtime_sec;
			int32_t mtime_nsec;
			uint64_t inode;

			size = file->size;
			mtime_sec = file->mtime_sec;
			mtime_nsec = file->mtime_nsec;
			inode = file->inode;

			sputc('f', f);
			sputb32(disk->mapping, f);
			sputb64(size, f);
			sputb64(mtime_sec, f);
			/* encode STAT_NSEC_INVALID as 0 */
			if (mtime_nsec == STAT_NSEC_INVALID)
				sputb32(0, f);
			else
				sputb32(mtime_nsec + 1, f);
			sputb64(inode, f);
			sputbs(file->sub, f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* for all the blocks of the file */
			begin = 0;
			while (begin < file->blockmax) {
				unsigned block_state = block_state_get(blockvec + begin);
				uint32_t v_pos = blockvec[begin].parity_pos;
				uint32_t v_count;
				
				block_off_t end;

				/* find the end of run of blocks */
				end = begin + 1;
				while (end < file->blockmax
					&& block_state == block_state_get(blockvec + end)
					&& blockvec[end-1].parity_pos + 1 == blockvec[end].parity_pos)
				{
					++end;
				}

				switch (block_state) {
				case BLOCK_STATE_BLK :
					sputc('b', f);
					break;
				case BLOCK_STATE_CHG :
					sputc('g', f);
					break;
				case BLOCK_STATE_REP :
					sputc('p', f);
					break;
				default:
					/* LCOV_EXCL_START */
					fprintf(stderr, "Internal state inconsistency in saving for block %u state %u\n", v_pos, block_state);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				sputb32(v_pos, f);

				v_count = end - begin;
				sputb32(v_count, f);

				for(b=begin;b<end;++b) {
					swrite(blockvec[b].hash, HASH_SIZE, f);
				}

				if (serror(f)) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				count_block += v_count;

				/* next begin position */
				begin = end;
			}

			++count_file;
		}

		/* for each link */
		for(j=disk->linklist;j!=0;j=j->next) {
			struct snapraid_link* link = j->data;

			switch (link_flag_get(link, FILE_IS_LINK_MASK)) {
			case FILE_IS_HARDLINK :
				sputc('a', f);
				++count_hardlink;
				break;
			case FILE_IS_SYMLINK :
				sputc('s', f);
				++count_symlink;
				break;
			}

			sputb32(disk->mapping, f);
			sputbs(link->sub, f);
			sputbs(link->linkto, f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		/* for each dir */
		for(j=disk->dirlist;j!=0;j=j->next) {
			struct snapraid_dir* dir = j->data;

			sputc('r', f);
			sputb32(disk->mapping, f);
			sputbs(dir->sub, f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			++count_dir;
		}

		/* deleted blocks of the disk */
		sputc('h', f);
		sputb32(disk->mapping, f);
		if (serror(f)) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		begin = 0;
		while (begin < blockmax) {
			int is_deleted;
			block_off_t end;

			is_deleted = is_block_deleted(disk, begin);

			/* find the end of run of blocks */
			end = begin + 1;
			while (end < blockmax
				&& is_deleted == is_block_deleted(disk, end))
			{
				++end;
			}

			sputb32(end - begin, f);

			if (is_deleted) {
				/* write the run of deleted blocks with hash */
				sputc('o', f);

				/* write all the hash */
				while (begin < end) {
					struct snapraid_block* block = disk_block_get(disk, begin);

					swrite(block->hash, HASH_SIZE, f);

					++begin;
				}
			} else {
				/* write the run of blocks without hash */
				/* they can be either used or empty blocks */
				sputc('O', f);

				/* next begin position */
				begin = end;
			}

			if (serror(f)) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/* write the info for each block */
	sputc('i', f);
	sputb32(info_oldest, f);
	begin = 0;
	while (begin < blockmax) {
		snapraid_info info;
		block_off_t end;
		time_t t;
		unsigned flag;

		info = info_get(&state->infoarr, begin);

		/* find the end of run of blocks */
		end = begin + 1;
		while (end < blockmax
			&& info == info_get(&state->infoarr, end))
		{
			++end;
		}

		sputb32(end - begin, f);

		/* if there is info */
		if (info) {
			/* other flags */
			flag = 1; /* info is present */
			if (info_get_bad(info))
				flag |= 2;
			if (info_get_rehash(info))
				flag |= 4;
			sputb32(flag, f);

			t = info_get_time(info) - info_oldest;
			sputb32(t, f);
		} else {
			/* write a special 0 flag to mark missing info */
			sputb32(0, f);
		}

		if (serror(f)) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* next begin position */
		begin = end;
	}

	sputc('N', f);
	sputble32(scrc(f), f);
	if (serror(f)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (state->opt.verbose) {
		printf("%8u files\n", count_file);
		printf("%8u blocks\n", count_block);
		printf("%8u hardlinks\n", count_hardlink);
		printf("%8u symlinks\n", count_symlink);
		printf("%8u empty dirs\n", count_dir);
	}
}

void state_read(struct snapraid_state* state)
{
	STREAM* f;
	char path[PATH_MAX];
	struct stat st;
	tommy_node* node;
	int ret;
	int c;

	/* iterate over all the available content files and load the first one present */
	f = 0;
	node = tommy_list_head(&state->contentlist);
	while (node) {
		struct snapraid_content* content = node->data;
		pathcpy(path, sizeof(path), content->content);

		if (!state->no_conf) {
			fprintf(stdlog, "content:%s\n", path);
			fflush(stdlog);
		}
		printf("Loading state from %s...\n", path);

		f = sopen_read(path);
		if (f != 0) {
			/* if openend stop the search */
			break;
		} else {
			/* if it's real error of an existing file, abort */
			if (errno != ENOENT) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error opening the content file '%s'. %s.\n", path, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* otherwise continue */
			if (node->next) {
				fprintf(stderr, "WARNING! Content file '%s' not found, trying with another copy...\n", path);

				/* ensure to rewrite all the content files */
				state->need_write = 1;
			}
		}

		/* next content file */
		node = node->next;
	}

	/* if not found, assume empty */
	if (!f) {
		fprintf(stderr, "No content file found. Assuming empty.\n");

		/* create the initial mapping */
		state_map(state);
		return;
	}

	/* get the stat of the content file */
	ret = fstat(shandle(f), &st);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error stating the content file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* go further to check other content files */
	while (node) {
		char other_path[PATH_MAX];
		struct stat other_st;
		struct snapraid_content* content = node->data;
		pathcpy(other_path, sizeof(other_path), content->content);

		ret = stat(other_path, &other_st);
		if (ret != 0) {
			/* allow missing content files, but not any other kind of error */
			if (errno != ENOENT) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error stating the content file '%s'. %s.\n", other_path, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* ensure to rewrite all the content files */
			state->need_write = 1;
		} else {
			/* if the size is different */
			if (other_st.st_size != st.st_size) {
				fprintf(stderr, "WARNING! Content files '%s' and '%s' have a different size!\n", path, other_path);
				fprintf(stderr, "Likely one of the two is broken!\n");
		
				/* ensure to rewrite all the content files */
				state->need_write = 1;
			}
		}

		/* next content file */
		node = node->next;
	}

	/* start with a undefined default. */
	/* it's for compatibility with version 1.0 where MD5 was implicit. */
	state->hash = HASH_UNDEFINED;

	/* start with a zero seed, it was the default in old versions */
	memset(state->hashseed, 0, HASH_SIZE);

	/* previous hash, start with an undefined value */
	state->prevhash = HASH_UNDEFINED;

	/* intentionally not set the prevhashseed, if used valgrind will warn about it */

	/* get the first char to detect the file type */
	c = sgetc(f);
	sungetc(c, f);

	/* guess the file type from the first char */
	if (c == 'S') {
		state_read_binary(state, path, f);
	} else {
		state_read_text(state, path, f, st.st_mtime);

		/* force a rewrite to convert to binary */
		state->need_write = 1;
	}

	sclose(f);

	if (state->hash == HASH_UNDEFINED) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "The checksum to use is not specified.\n");
		fprintf(stderr, "This happens because you are likely upgrading from SnapRAID 1.0.\n");
		fprintf(stderr, "To use a new SnapRAID you must restart from scratch,\n");
		fprintf(stderr, "deleting all the content and parity files.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* update the mapping */
	state_map(state);

	state_content_check(state, path);
}

void state_write(struct snapraid_state* state)
{
	STREAM* f;
	unsigned count_content;
	tommy_node* i;
	unsigned k;

	/* count the content files */
	count_content = 0;
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		printf("Saving state to %s...\n", content->content);
		++count_content;
		i = i->next;
	}

	/* open all the content files */
	f = sopen_multi_write(count_content);
	if (!f) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error opening the content files.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	k = 0;
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		char tmp[PATH_MAX];
		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);
		if (sopen_multi_file(f, k, tmp) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error opening the content file '%s'. %s.\n", tmp, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		++k;
		i = i->next;
	}

	if (state->opt.force_content_text)
		state_write_text(state, f);
	else
		state_write_binary(state, f);

	/* Use the sequence fflush() -> fsync() -> fclose() -> rename() to ensure */
	/* than even in a system crash event we have one valid copy of the file. */

	if (sflush(f) != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing the content file '%s', in flush(). %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

#if HAVE_FSYNC
	if (ssync(f) != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing the content file '%s' in sync(). %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
#endif

	if (sclose(f) != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing the content file '%s' in close(). %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		char tmp[PATH_MAX];
		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);
		if (rename(tmp, content->content) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error renaming the content file '%s' to '%s' in rename(). %s.\n", tmp, content->content, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		i = i->next;
	}

	state->need_write = 0; /* no write needed anymore */
}

void state_filter(struct snapraid_state* state, tommy_list* filterlist_file, tommy_list* filterlist_disk, int filter_missing, int filter_error)
{
	tommy_node* i;

	/* if no filter, include all */
	if (!filter_missing && !filter_error && tommy_list_empty(filterlist_file) && tommy_list_empty(filterlist_disk))
		return;

	printf("Filtering...\n");

	if (state->opt.verbose) {
		tommy_node* k;
		for(k=tommy_list_head(filterlist_disk);k!=0;k=k->next) {
			struct snapraid_filter* filter = k->data;
			printf("\t%s", filter->pattern);
			if (filter->is_disk)
				printf("//");
			printf("\n");
		}
		for(k=tommy_list_head(filterlist_file);k!=0;k=k->next) {
			struct snapraid_filter* filter = k->data;
			printf("\t%s", filter->pattern);
			if (filter->is_dir)
				printf("/");
			printf("\n");
		}
		if (filter_missing)
			printf("\t<missing>\n");
		if (filter_error)
			printf("\t<error>\n");
	}

	/* for each disk */
	for(i=state->disklist;i!=0;i=i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* if we filter for presence, we have to access the disk, so better to print something */
		if (filter_missing)
			printf("Scanning disk %s...\n", disk->name);

		/* for each file */
		for(j=tommy_list_head(&disk->filelist);j!=0;j=j->next) {
			struct snapraid_file* file = j->data;

			if (filter_path(filterlist_disk, disk->name, file->sub) != 0
				|| filter_path(filterlist_file, disk->name, file->sub) != 0
				|| filter_existence(filter_missing, disk->dir, file->sub) != 0
				|| filter_correctness(filter_error, &state->infoarr, file) != 0
			) {
				file_flag_set(file, FILE_IS_EXCLUDED);
			}
		}

		/* for each link */
		for(j=tommy_list_head(&disk->linklist);j!=0;j=j->next) {
			struct snapraid_link* link = j->data;

			if (filter_path(filterlist_disk, disk->name, link->sub) != 0
				|| filter_path(filterlist_file, disk->name, link->sub) != 0
				|| filter_existence(filter_missing, disk->dir, link->sub) != 0
			) {
				link_flag_set(link, FILE_IS_EXCLUDED);
			}
		}

		/* for each dir */
		for(j=tommy_list_head(&disk->dirlist);j!=0;j=j->next) {
			struct snapraid_dir* dir = j->data;

			if (filter_dir(filterlist_disk, disk->name, dir->sub) != 0
				|| filter_dir(filterlist_file, disk->name, dir->sub) != 0
				|| filter_existence(filter_missing, disk->dir, dir->sub) != 0
			) {
				dir_flag_set(dir, FILE_IS_EXCLUDED);
			}
		}
	}
}

int state_progress_begin(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax, block_off_t countmax)
{
	if (state->opt.gui) {
		fprintf(stdlog,"run:begin:%u:%u:%u\n", blockstart, blockmax, countmax);
		fflush(stdlog);
	} else {
		time_t now;

		now = time(0);

		state->progress_start = now;
		state->progress_last = now;
		state->progress_subtract = 0;
	}

	/* stop if requested */
	if (global_interrupt) {
		if (!state->opt.gui) {
			printf("Not starting for interruption\n");
		}
		return 0;
	}

	return 1;
}

void state_progress_end(struct snapraid_state* state, block_off_t countpos, block_off_t countmax, data_off_t countsize)
{
	if (state->opt.gui) {
		fprintf(stdlog, "run:end\n");
		fflush(stdlog);
	} else {
		time_t now;
		time_t elapsed;

		unsigned countsize_MiB = (countsize + 1024*1024 - 1) / (1024*1024);

		now = time(0);

		elapsed = now - state->progress_start - state->progress_subtract;

		if (countmax) {
			printf("%u%% completed, %u MiB processed", countpos * 100 / countmax, countsize_MiB);
			if (elapsed >= 60)
				printf(" in %u:%02u", (unsigned)(elapsed / 3600), (unsigned)((elapsed % 3600) / 60));
			printf("\n");
		} else {
			printf("Nothing to do\n");
		}
	}
}

#define PROGRESS_CLEAR "          "

void state_progress_stop(struct snapraid_state* state)
{
	time_t now;

	now = time(0);

	printf("\n");
	
	state->progress_interruption = now;
}

void state_progress_restart(struct snapraid_state* state)
{
	time_t now;

	now = time(0);

	if (now >= state->progress_interruption) /* avoid degenerated cases when the clock is manually adjusted */
		state->progress_subtract += now - state->progress_interruption;
}

int state_progress(struct snapraid_state* state, block_off_t blockpos, block_off_t countpos, block_off_t countmax, data_off_t countsize)
{
	if (state->opt.gui) {
		fprintf(stdlog, "run:pos:%u:%u:%"PRIu64"\n", blockpos, countpos, countsize);
		fflush(stdlog);
	} else {
		time_t now;

		now = time(0);

		if (state->progress_last != now) {
			time_t delta = now - state->progress_start - state->progress_subtract;

			printf("%u%%, %u MiB", countpos * 100 / countmax, (unsigned)(countsize / (1024*1024)));

			if (delta != 0) {
				printf(", %u MiB/s", (unsigned)(countsize / (1024*1024) / delta));
			}

			if (delta > 5 && countpos > 0) {
				unsigned m, h;
				data_off_t to_do = countmax - countpos;

				m = to_do * delta / (60 * countpos);

				h = m / 60;
				m = m % 60;

				printf(", %u:%02u ETA%s", h, m, PROGRESS_CLEAR);
			}
			printf("\r");
			fflush(stdout);
			state->progress_last = now;
		}
	}

	/* stop if requested */
	if (global_interrupt) {
		if (!state->opt.gui) {
			printf("\n");
			printf("Stopping for interruption at block %u\n", blockpos);
		}
		return 1;
	}

	return 0;
}

void generate_configuration(const char* path)
{
	struct snapraid_state state;
	struct snapraid_content* content;
	unsigned i;
	tommy_node* j;

	state_init(&state);

	/* mark that we are without a configuration file */
	state.no_conf = 1;

	/* create the dummy content entry */
	content = content_alloc(path, -1);

	/* adds the content entry */
	tommy_list_insert_tail(&state.contentlist, &content->node, content);

	/* read the content file */
	state_read(&state);

	/* output a dummy configuration file */
	printf("# Configuration file generated from %s\n", path);
	printf("\n");
	printf("# Use this blocksize\n");
	printf("blocksize %u\n", state.block_size / 1024);
	printf("\n");
	for(i=0;i<LEV_MAX;++i) {
		printf("# Set the correct path for the %s file%s\n", lev_name(i), i != 0 ? " (if used)" : "");
		printf("%s ENTER_HERE_THE_PARITY_FILE\n", lev_config_name(i));
		printf("\n");
	}
	printf("# Add any other content file\n");
	printf("content %s\n", path);
	printf("\n");
	for(j=state.maplist;j;j=j->next) {
		struct snapraid_map* map = j->data;
		struct snapraid_disk* disk;
		printf("# Set the correct dir for disk '%s'\n", map->name);
		printf("# Disk '%s' is the one with system id '%s'\n", map->name, map->uuid);
		disk = find_disk(&state, map->name);
		if (disk && disk->filelist) {
			struct snapraid_file* file = disk->filelist->data;
			if (file) {
				printf("# and containing: %s\n", file->sub);
			}
		}
		printf("disk %s ENTER_HERE_THE_DIR\n", map->name);
		printf("\n");
	}

	state_done(&state);
}

