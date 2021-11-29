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
#include "search.h"
#include "state.h"
#include "support.h"
#include "parity.h"
#include "stream.h"
#include "handle.h"
#include "io.h"
#include "raid/raid.h"
#include "raid/cpu.h"

/**
 * Configure the multithread support.
 *
 * Multi thread for write could be either faster or slower, depending
 * on the specific conditions. With multithreads it's likely faster
 * writing to disk, but you'll need to access multiple times the same data,
 * being potentially slower.
 *
 * Multi thread for verify is instead always generally faster,
 * so we enable it if possible.
 */
#if HAVE_THREAD
/* #define HAVE_MT_WRITE 1 */
#define HAVE_MT_VERIFY 1
#endif

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

static int lev_config_scan(const char* s, unsigned* level, unsigned* mode)
{
	if (strcmp(s, "parity") == 0 || strcmp(s, "1-parity") == 0) {
		*level = 0;
		return 0;
	}

	if (strcmp(s, "q-parity") == 0 || strcmp(s, "2-parity") == 0) {
		*level = 1;
		return 0;
	}

	if (strcmp(s, "r-parity") == 0 || strcmp(s, "3-parity") == 0) {
		*level = 2;
		return 0;
	}

	if (strcmp(s, "4-parity") == 0) {
		*level = 3;
		return 0;
	}

	if (strcmp(s, "5-parity") == 0) {
		*level = 4;
		return 0;
	}

	if (strcmp(s, "6-parity") == 0) {
		*level = 5;
		return 0;
	}

	if (strcmp(s, "z-parity") == 0) {
		*level = 2;
		if (mode)
			*mode = RAID_MODE_VANDERMONDE;
		return 0;
	}

	return -1;
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
	unsigned l, s;

	memset(&state->opt, 0, sizeof(state->opt));
	state->filter_hidden = 0;
	state->autosave = 0;
	state->need_write = 0;
	state->checked_read = 0;
	state->block_size = 256 * KIBI; /* default 256 KiB */
	state->raid_mode = RAID_MODE_CAUCHY;
	state->file_mode = ADVISE_DEFAULT;
	for (l = 0; l < LEV_MAX; ++l) {
		state->parity[l].split_mac = 0;
		for (s = 0; s < SPLIT_MAX; ++s) {
			state->parity[l].split_map[s].path[0] = 0;
			state->parity[l].split_map[s].uuid[0] = 0;
			state->parity[l].split_map[s].size = PARITY_SIZE_INVALID;
			state->parity[l].split_map[s].device = 0;
		}
		state->parity[l].smartctl[0] = 0;
		state->parity[l].total_blocks = 0;
		state->parity[l].free_blocks = 0;
		state->parity[l].skip_access = 0;
		state->parity[l].tick = 0;
		state->parity[l].cached_blocks = 0;
		state->parity[l].is_excluded_by_filter = 0;
	}
	state->tick_io = 0;
	state->tick_misc = 0;
	state->tick_sched = 0;
	state->tick_raid = 0;
	state->tick_hash = 0;
	state->tick_last = tick();
	state->share[0] = 0;
	state->pool[0] = 0;
	state->pool_device = 0;
	state->lockfile[0] = 0;
	state->level = 1; /* default is the lowest protection */
	state->clear_past_hash = 0;
	state->no_conf = 0;

	tommy_list_init(&state->disklist);
	tommy_list_init(&state->maplist);
	tommy_list_init(&state->contentlist);
	tommy_list_init(&state->filterlist);
	tommy_list_init(&state->importlist);
	tommy_hashdyn_init(&state->importset);
	tommy_hashdyn_init(&state->previmportset);
	tommy_hashdyn_init(&state->searchset);
	tommy_arrayblkof_init(&state->infoarr, sizeof(snapraid_info));
}

void state_done(struct snapraid_state* state)
{
	tommy_list_foreach(&state->disklist, (tommy_foreach_func*)disk_free);
	tommy_list_foreach(&state->maplist, (tommy_foreach_func*)map_free);
	tommy_list_foreach(&state->contentlist, (tommy_foreach_func*)content_free);
	tommy_list_foreach(&state->filterlist, (tommy_foreach_func*)filter_free);
	tommy_list_foreach(&state->importlist, (tommy_foreach_func*)import_file_free);
	tommy_hashdyn_foreach(&state->searchset, (tommy_foreach_func*)search_file_free);
	tommy_hashdyn_done(&state->importset);
	tommy_hashdyn_done(&state->previmportset);
	tommy_hashdyn_done(&state->searchset);
	tommy_arrayblkof_done(&state->infoarr);
}

/**
 * Check the configuration.
 */
static void state_config_check(struct snapraid_state* state, const char* path, tommy_list* filterlist_disk)
{
	tommy_node* i;
	unsigned l, s;

	/* check for parity level */
	if (state->raid_mode == RAID_MODE_VANDERMONDE) {
		if (state->level > 3) {
			/* LCOV_EXCL_START */
			log_fatal("If you use the z-parity you cannot have more than 3 parities.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STIO */
		}
	}

	for (l = 0; l < state->level; ++l) {
		if (state->parity[l].split_mac == 0) {
			/* LCOV_EXCL_START */
			log_fatal("No '%s' specification in '%s'\n", lev_config_name(l), path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	if (tommy_list_empty(&state->contentlist)) {
		/* LCOV_EXCL_START */
		log_fatal("No 'content' specification in '%s'\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* check for equal paths */
	for (i = state->contentlist; i != 0; i = i->next) {
		struct snapraid_content* content = i->data;

		for (l = 0; l < state->level; ++l) {
			for (s = 0; s < state->parity[l].split_mac; ++s) {
				if (pathcmp(state->parity[l].split_map[s].path, content->content) == 0) {
					/* LCOV_EXCL_START */
					log_fatal("Same path used for '%s' and 'content' as '%s'\n", lev_config_name(l), content->content);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
		}
	}

	/* check device of data disks */
	if (!state->opt.skip_device && !state->opt.skip_disk_access) {
		for (i = state->disklist; i != 0; i = i->next) {
			tommy_node* j;
			struct snapraid_disk* disk = i->data;

			/* skip data disks that are not accessible */
			if (disk->skip_access)
				continue;

#ifdef _WIN32
			if (disk->device == 0) {
				/* LCOV_EXCL_START */
				log_fatal("Disk '%s' has a zero serial number.\n", disk->dir);
				log_fatal("This is not necessarily wrong, but for using SnapRAID\n");
				log_fatal("it's better to change the serial number of the disk.\n");
				log_fatal("Try using the 'VolumeID' tool by 'Mark Russinovich'.\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
#endif

			for (j = i->next; j != 0; j = j->next) {
				struct snapraid_disk* other = j->data;
				if (disk->device == other->device) {
					if (state->opt.force_device) {
						/* note that we just ignore the issue */
						/* and we DON'T mark the disk to be skipped */
						/* because we want to use these disks */
						if (!state->opt.no_warnings)
							log_fatal("DANGER! Ignoring that disks '%s' and '%s' are on the same device\n", disk->name, other->name);
					} else {
						/* LCOV_EXCL_START */
						log_fatal("Disks '%s' and '%s' are on the same device.\n", disk->dir, other->dir);
#ifdef _WIN32
						log_fatal("Both have the serial number '%" PRIx64 "'.\n", disk->device);
						log_fatal("Try using the 'VolumeID' tool by 'Mark Russinovich'\n");
						log_fatal("to change one of the disk serial.\n");
#endif
						/* in "fix" we allow to continue anyway */
						if (strcmp(state->command, "fix") == 0) {
							log_fatal("You can '%s' anyway, using 'snapraid --force-device %s'.\n", state->command, state->command);
						}
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}
				}
			}

			/* skip data disks that are not accessible */
			if (disk->skip_access)
				continue;

			if (!state->opt.skip_parity_access) {
				for (l = 0; l < state->level; ++l) {
					for (s = 0; s < state->parity[l].split_mac; ++s) {
						if (disk->device == state->parity[l].split_map[s].device) {
							if (state->opt.force_device) {
								/* note that we just ignore the issue */
								/* and we DON'T mark the disk to be skipped */
								/* because we want to use these disks */
								if (!state->opt.no_warnings)
									log_fatal("DANGER! Ignoring that disks '%s' and %s '%s' are on the same device\n", disk->dir, lev_name(l), state->parity[l].split_map[s].path);
							} else {
								/* LCOV_EXCL_START */
								log_fatal("Disk '%s' and %s '%s' are on the same device.\n", disk->dir, lev_name(l), state->parity[l].split_map[s].path);
#ifdef _WIN32
								log_fatal("Both have the serial number '%" PRIx64 "'.\n", disk->device);
								log_fatal("Try using the 'VolumeID' tool by 'Mark Russinovich'\n");
								log_fatal("to change one of the disk serial.\n");
#endif
								exit(EXIT_FAILURE);
								/* LCOV_EXCL_STOP */
							}
						}
					}
				}
			}

			if (state->pool[0] != 0 && disk->device == state->pool_device) {
				/* LCOV_EXCL_START */
				log_fatal("Disk '%s' and pool '%s' are on the same device.\n", disk->dir, state->pool);
#ifdef _WIN32
				log_fatal("Both have the serial number '%" PRIx64 "'.\n", disk->device);
				log_fatal("Try using the 'VolumeID' tool by 'Mark Russinovich'\n");
				log_fatal("to change one of the disk serial.\n");
#endif
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/* check device of parity disks */
	if (!state->opt.skip_device && !state->opt.skip_parity_access) {
		for (l = 0; l < state->level; ++l) {
			for (s = 0; s < state->parity[l].split_mac; ++s) {
				unsigned j, t;

				/* skip parity disks that are not accessible */
				if (state->parity[l].skip_access)
					continue;

#ifdef _WIN32
				if (state->parity[l].split_map[s].device == 0) {
					/* LCOV_EXCL_START */
					log_fatal("Disk '%s' has a zero serial number.\n", state->parity[l].split_map[s].path);
					log_fatal("This is not necessarily wrong, but for using SnapRAID\n");
					log_fatal("it's better to change the serial number of the disk.\n");
					log_fatal("Try using the 'VolumeID' tool by 'Mark Russinovich'.\n");
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
#endif

				for (j = l + 1; j < state->level; ++j) {
					for (t = 0; t < state->parity[j].split_mac; ++t) {
						if (state->parity[l].split_map[s].device == state->parity[j].split_map[t].device) {
							if (state->opt.force_device) {
								/* note that we just ignore the issue */
								/* and we DON'T mark the disk to be skipped */
								/* because we want to use these disks */
								if (!state->opt.no_warnings)
									log_fatal("DANGER! Skipping parities '%s' and '%s' on the same device\n", lev_config_name(l), lev_config_name(j));
							} else {
								/* LCOV_EXCL_START */
								log_fatal("Parity '%s' and '%s' are on the same device.\n", state->parity[l].split_map[s].path, state->parity[j].split_map[t].path);
#ifdef _WIN32
								log_fatal("Both have the serial number '%" PRIx64 "'.\n", state->parity[l].split_map[s].device);
								log_fatal("Try using the 'VolumeID' tool by 'Mark Russinovich'\n");
								log_fatal("to change one of the disk serial.\n");
#endif
								/* in "fix" we allow to continue anyway */
								if (strcmp(state->command, "fix") == 0) {
									log_fatal("You can '%s' anyway, using 'snapraid --force-device %s'.\n", state->command, state->command);
								}
								exit(EXIT_FAILURE);
								/* LCOV_EXCL_STOP */
							}
						}
					}
				}

				if (state->pool[0] != 0 && state->pool_device == state->parity[l].split_map[s].device) {
					/* LCOV_EXCL_START */
					log_fatal("Pool '%s' and parity '%s' are on the same device.\n", state->pool, state->parity[l].split_map[s].path);
#ifdef _WIN32
					log_fatal("Both have the serial number '%" PRIx64 "'.\n", state->pool_device);
					log_fatal("Try using the 'VolumeID' tool by 'Mark Russinovich'\n");
					log_fatal("to change one of the disk serial.\n");
#endif
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
		}
	}

	/* check device of pool disk */
#ifdef _WIN32
	if (!state->opt.skip_device) {
		if (state->pool[0] != 0 && state->pool_device == 0) {
			/* LCOV_EXCL_START */
			log_fatal("Disk '%s' has a zero serial number.\n", state->pool);
			log_fatal("This is not necessarily wrong, but for using SnapRAID\n");
			log_fatal("it's better to change the serial number of the disk.\n");
			log_fatal("Try using the 'VolumeID' tool by 'Mark Russinovich'.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	/* count the content files */
	if (!state->opt.skip_device && !state->opt.skip_content_access) {
		unsigned content_count;

		content_count = 0;
		for (i = state->contentlist; i != 0; i = i->next) {
			tommy_node* j;
			struct snapraid_content* content = i->data;

			/* check if there are others in the same disk */
			for (j = i->next; j != 0; j = j->next) {
				struct snapraid_content* other = j->data;
				if (content->device == other->device) {
					log_fatal("WARNING! Content files on the same disk: '%s' and '%s'.\n", content->content, other->content);
					break;
				}
			}
			if (j != 0) {
				/* skip it */
				continue;
			}

			++content_count;
		}

		if (content_count < state->level + 1) {
			/* LCOV_EXCL_START */
			log_fatal("You must have at least %d 'content' files in different disks.\n", state->level + 1);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	/* check for speed */
#ifdef CONFIG_X86
	if (!raid_cpu_has_ssse3())
#endif
	if (state->raid_mode == RAID_MODE_CAUCHY && !state->opt.no_warnings) {
		if (state->level == 3) {
			log_fatal("WARNING! Your CPU doesn't have a fast implementation for triple parity.\n");
			log_fatal("WARNING! It's recommended to switch to 'z-parity' instead than '3-parity'.\n");
		} else if (state->level > 3) {
			log_fatal("WARNING! Your CPU doesn't have a fast implementation beyond triple parity.\n");
			log_fatal("WARNING! It's recommended to reduce the parity levels to triple parity.\n");
		}
	}

	/* ensure that specified filter disks are valid ones */
	for (i = tommy_list_head(filterlist_disk); i != 0; i = i->next) {
		tommy_node* j;
		struct snapraid_filter* filter = i->data;
		for (j = state->disklist; j != 0; j = j->next) {
			struct snapraid_disk* disk = j->data;
			if (fnmatch(filter->pattern, disk->name, FNM_CASEINSENSITIVE_FOR_WIN) == 0)
				break;
		}
		if (j == 0) {
			/* check matching with parity disks */
			for (l = 0; l < state->level; ++l)
				if (fnmatch(filter->pattern, lev_config_name(l), FNM_CASEINSENSITIVE_FOR_WIN) == 0)
					break;
			if (l == state->level) {
				/* LCOV_EXCL_START */
				log_fatal("Option -d, --filter-disk %s doesn't match any data or parity disk.\n", filter->pattern);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}
}

/**
 * Validate the smartctl command.
 *
 * It must contains only one %s string, and not other % chars.
 */
static int validate_smartctl(const char* custom)
{
	const char* s = custom;
	int arg = 0;

	while (*s) {
		if (s[0] == '%' && s[1] == 's') {
			if (arg) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			arg = 1;
		} else if (s[0] == '%') {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}

		++s;
	}

	return 0;
}

void state_config(struct snapraid_state* state, const char* path, const char* command, struct snapraid_option* opt, tommy_list* filterlist_disk)
{
	STREAM* f;
	unsigned line;
	tommy_node* i;
	unsigned l, s;

	/* copy the options */
	state->opt = *opt;

	/* if unset, sort by physical order */
	if (!state->opt.force_order)
		state->opt.force_order = SORT_PHYSICAL;

	/* adjust file mode */
	if (state->opt.file_mode != ADVISE_DEFAULT) {
		state->file_mode = state->opt.file_mode;
	} else {
		/* default mode, if nothing is specified */
		state->file_mode = ADVISE_DISCARD;
	}

	/* store current command */
	state->command = command;

	log_tag("conf:file:%s\n", path);

	f = sopen_read(path);
	if (!f) {
		/* LCOV_EXCL_START */
		if (errno == ENOENT) {
			if (state->opt.auto_conf) {
				log_tag("conf:missing:\n");
				/* mark that we are without a configuration file */
				state->no_conf = 1;
				state->level = 0;
				return;
			}

			log_fatal("No configuration file found at '%s'\n", path);
		} else if (errno == EACCES) {
			log_fatal("You do not have rights to access the configuration file '%s'\n", path);
		} else {
			log_fatal("Error opening the configuration file '%s'. %s.\n", path, strerror(errno));
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
		unsigned level;

		/* skip initial spaces */
		sgetspace(f);

		/* read the command */
		ret = sgettok(f, tag, sizeof(tag));
		if (ret < 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error reading the configuration file '%s' at line %u\n", path, line);
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
				log_fatal("Invalid 'blocksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (state->block_size < 1) {
				/* LCOV_EXCL_START */
				log_fatal("Too small 'blocksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (state->block_size > 16 * KIBI) {
				/* LCOV_EXCL_START */
				log_fatal("Too big 'blocksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			/* check if it's a power of 2 */
			if ((state->block_size & (state->block_size - 1)) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Not power of 2 'blocksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			state->block_size *= KIBI;
		} else if (strcmp(tag, "hashsize") == 0
			|| strcmp(tag, "hash_size") == 0 /* v11.0 used incorrectly this one, kept now for backward compatibility */
		) {
			uint32_t hash_size;

			ret = sgetu32(f, &hash_size);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'hashsize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (hash_size < 2) {
				/* LCOV_EXCL_START */
				log_fatal("Too small 'hashsize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (hash_size > HASH_MAX) {
				/* LCOV_EXCL_START */
				log_fatal("Too big 'hashsize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			/* check if it's a power of 2 */
			if ((hash_size & (hash_size - 1)) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Not power of 2 'hashsize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			BLOCK_HASH_SIZE = hash_size;
		} else if (lev_config_scan(tag, &level, &state->raid_mode) == 0) {
			char device[PATH_MAX];
			char* split_map[SPLIT_MAX + 1];
			unsigned split_mac;
			char* slash;
			uint64_t dev;
			int skip_access;

			if (state->parity[level].split_mac != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Multiple '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				log_fatal("Empty '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			split_mac = strsplit(split_map, SPLIT_MAX + 1, buffer, ",");

			if (split_mac > SPLIT_MAX) {
				/* LCOV_EXCL_START */
				log_fatal("Too many files in '%s' specification in '%s' at line %u\n", tag, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			skip_access = 0;
			state->parity[level].split_mac = split_mac;
			for (s = 0; s < split_mac; ++s) {
				pathimport(state->parity[level].split_map[s].path, sizeof(state->parity[level].split_map[s].path), split_map[s]);

				if (!state->opt.skip_parity_access) {
					struct stat st;

					/* get the device of the directory containing the parity file */
					pathimport(device, sizeof(device), split_map[s]);
					slash = strrchr(device, '/');
					if (slash)
						*slash = 0;
					else
						pathcpy(device, sizeof(device), ".");

					if (stat(device, &st) == 0) {
						dev = st.st_dev;
					} else {
						/* if the disk can be skipped */
						if (state->opt.force_device) {
							/* use a fake device, and mark the disk to be skipped */
							dev = 0;
							skip_access = 1;
							log_fatal("DANGER! Skipping inaccessible parity disk '%s'...\n", tag);
						} else {
							/* LCOV_EXCL_START */
							log_fatal("Error accessing 'parity' dir '%s' specification in '%s' at line %u\n", device, path, line);

							/* in "fix" we allow to continue anyway */
							if (strcmp(state->command, "fix") == 0) {
								log_fatal("You can '%s' anyway, using 'snapraid --force-device %s'.\n", state->command, state->command);
							}
							exit(EXIT_FAILURE);
							/* LCOV_EXCL_STOP */
						}
					}
				} else {
					/* use a fake device */
					dev = 0;
				}

				state->parity[level].split_map[s].device = dev;
			}

			/* store the global parity skip_access */
			state->parity[level].skip_access = skip_access;

			/* adjust the level */
			if (state->level < level + 1)
				state->level = level + 1;
		} else if (strcmp(tag, "share") == 0) {
			if (*state->share) {
				/* LCOV_EXCL_START */
				log_fatal("Multiple 'share' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'share' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'share' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			pathimport(state->share, sizeof(state->share), buffer);
		} else if (strcmp(tag, "pool") == 0) {
			struct stat st;

			if (*state->pool) {
				/* LCOV_EXCL_START */
				log_fatal("Multiple 'pool' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'pool' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'pool' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			pathimport(state->pool, sizeof(state->pool), buffer);

			/* get the device of the directory containing the pool tree */
			if (stat(buffer, &st) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error accessing 'pool' dir '%s' specification in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			state->pool_device = st.st_dev;
		} else if (strcmp(tag, "content") == 0) {
			struct snapraid_content* content;
			char device[PATH_MAX];
			char* slash;
			struct stat st;
			uint64_t dev;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (pathcmp(buffer, "/dev/null") == 0 || pathcmp(buffer, "NUL") == 0) {
				/* LCOV_EXCL_START */
				log_fatal("You cannot use the null device as 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* check if the content file is already specified */
			for (i = state->contentlist; i != 0; i = i->next) {
				content = i->data;
				if (pathcmp(content->content, buffer) == 0)
					break;
			}
			if (i) {
				/* LCOV_EXCL_START */
				log_fatal("Duplicate 'content' specification in '%s' at line %u\n", path, line);
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
			if (stat(device, &st) == 0) {
				dev = st.st_dev;
			} else {
				if (state->opt.skip_content_access) {
					/* use a fake device */
					dev = 0;
					log_fatal("WARNING! Skipping inaccessible content file '%s'...\n", buffer);
				} else {
					/* LCOV_EXCL_START */
					log_fatal("Error accessing 'content' dir '%s' specification in '%s' at line %u\n", device, path, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}

			/* set the lock file at the first accessible content file */
			if (state->lockfile[0] == 0 && dev != 0) {
				pathcpy(state->lockfile, sizeof(state->lockfile), buffer);
				pathcat(state->lockfile, sizeof(state->lockfile), ".lock");
			}

			content = content_alloc(buffer, dev);

			tommy_list_insert_tail(&state->contentlist, &content->node, content);
		} else if (strcmp(tag, "data") == 0 || strcmp(tag, "disk") == 0) {
			/* "disk" is the deprecated name up to SnapRAID 9.x */
			char dir[PATH_MAX];
			char device[PATH_MAX];
			char uuid[UUID_MAX];
			struct snapraid_disk* disk;
			uint64_t dev;
			int skip_access;

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'data' name specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'data' name specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			sgetspace(f);

			ret = sgetlasttok(f, dir, sizeof(dir));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'data' dir specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*dir) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'data' dir specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* get the device of the dir */
			pathimport(device, sizeof(device), dir);

			/* check if the disk name already exists */
			for (i = state->disklist; i != 0; i = i->next) {
				disk = i->data;
				if (strcmp(disk->name, buffer) == 0)
					break;
			}
			if (i) {
				/* LCOV_EXCL_START */
				log_fatal("Duplicate 'data' name '%s' at line %u\n", buffer, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* if the disk has to be present */
			skip_access = 0;
			if (!state->opt.skip_disk_access) {
				struct stat st;

				if (stat(device, &st) == 0) {
					dev = st.st_dev;

					/* read the uuid, if unsupported use an empty one */
					if (devuuid(dev, uuid, sizeof(uuid)) != 0) {
						*uuid = 0;
					}

					/* fake a different UUID when testing */
					if (state->opt.fake_uuid) {
						snprintf(uuid, sizeof(uuid), "fake-uuid-%d", state->opt.fake_uuid);
						--state->opt.fake_uuid;
					}
				} else {
					/* if the disk can be skipped */
					if (state->opt.force_device) {
						/* use a fake device, and mark the disk to be skipped */
						dev = 0;
						*uuid = 0;
						skip_access = 1;
						log_fatal("DANGER! Skipping inaccessible data disk '%s'...\n", buffer);
					} else {
						/* LCOV_EXCL_START */
						log_fatal("Error accessing 'disk' '%s' specification in '%s' at line %u\n", dir, device, line);

						/* in "fix" we allow to continue anyway */
						if (strcmp(state->command, "fix") == 0) {
							log_fatal("You can '%s' anyway, using 'snapraid --force-device %s'.\n", state->command, state->command);
						}
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}
				}
			} else {
				/* use a fake device */
				dev = 0;
				*uuid = 0;
			}

			disk = disk_alloc(buffer, dir, dev, uuid, skip_access);

			tommy_list_insert_tail(&state->disklist, &disk->node, disk);
		} else if (strcmp(tag, "smartctl") == 0) {
			char custom[PATH_MAX];

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'smartctl' name specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'smartctl' name specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			sgetspace(f);

			ret = sgetlasttok(f, custom, sizeof(custom));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'smartctl' option specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*custom) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'smartctl' option specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (validate_smartctl(custom) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'smartctl' option specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* search for parity */
			if (lev_config_scan(buffer, &level, 0) == 0) {
				if (state->parity[level].smartctl[0] != 0) {
					/* LCOV_EXCL_START */
					log_fatal("Duplicate parity smartctl '%s' at line %u\n", buffer, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				pathcpy(state->parity[level].smartctl, sizeof(state->parity[level].smartctl), custom);
			} else {
				struct snapraid_disk* disk;

				/* search the disk */
				disk = 0;
				for (i = state->disklist; i != 0; i = i->next) {
					disk = i->data;
					if (strcmp(disk->name, buffer) == 0)
						break;
				}
				if (!disk) {
					/* LCOV_EXCL_START */
					log_fatal("Missing disk smartctl '%s' at line %u\n", buffer, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
				if (disk->smartctl[0] != 0) {
					/* LCOV_EXCL_START */
					log_fatal("Duplicate disk name '%s' at line %u\n", buffer, line);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				pathcpy(disk->smartctl, sizeof(disk->smartctl), custom);
			}
		} else if (strcmp(tag, "nohidden") == 0) {
			state->filter_hidden = 1;
		} else if (strcmp(tag, "exclude") == 0) {
			struct snapraid_filter* filter;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'exclude' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'exclude' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			filter = filter_alloc_file(-1, buffer);
			if (!filter) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'exclude' specification '%s' in '%s' at line %u\n", buffer, path, line);
				log_fatal("Filters using relative paths are not supported. Ensure to add an initial slash\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			tommy_list_insert_tail(&state->filterlist, &filter->node, filter);
		} else if (strcmp(tag, "include") == 0) {
			struct snapraid_filter* filter;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'include' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'include' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			filter = filter_alloc_file(1, buffer);
			if (!filter) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'include' specification '%s' in '%s' at line %u\n", buffer, path, line);
				log_fatal("Filters using relative paths are not supported. Ensure to add an initial slash\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			tommy_list_insert_tail(&state->filterlist, &filter->node, filter);
		} else if (strcmp(tag, "autosave") == 0) {
			char* e;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'autosave' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (!*buffer) {
				/* LCOV_EXCL_START */
				log_fatal("Empty 'autosave' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			state->autosave = strtoul(buffer, &e, 0);

			if (!e || *e) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid 'autosave' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* convert to GB */
			state->autosave *= GIGA;
		} else if (tag[0] == 0) {
			/* allow empty lines */
		} else if (tag[0] == '#') {
			ret = sgetline(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid comment in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else {
			/* LCOV_EXCL_START */
			log_fatal("Invalid command '%s' in '%s' at line %u\n", tag, path, line);
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
			log_fatal("Extra data in '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		++line;
	}

	if (serror(f)) {
		/* LCOV_EXCL_START */
		log_fatal("Error reading the configuration file '%s' at line %u\n", path, line);
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
	if (randomize(state->hashseed, HASH_MAX) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to get random values.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* no previous hash by default */
	state->prevhash = HASH_UNDEFINED;

	/* intentionally not set the prevhashseed, if used valgrind will warn about it */

	log_tag("blocksize:%u\n", state->block_size);
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		log_tag("data:%s:%s\n", disk->name, disk->dir);
	}

	log_tag("mode:%s\n", lev_raid_name(state->raid_mode, state->level));
	for (l = 0; l < state->level; ++l)
		for (s = 0; s < state->parity[l].split_mac; ++s)
			log_tag("%s:%u:%s\n", lev_config_name(l), s, state->parity[l].split_map[s].path);
	if (state->pool[0] != 0)
		log_tag("pool:%s\n", state->pool);
	if (state->share[0] != 0)
		log_tag("share:%s\n", state->share);
	if (state->autosave != 0)
		log_tag("autosave:%" PRIu64 "\n", state->autosave);
	for (i = tommy_list_head(&state->filterlist); i != 0; i = i->next) {
		char out[PATH_MAX];
		struct snapraid_filter* filter = i->data;
		log_tag("filter:%s\n", filter_type(filter, out, sizeof(out)));
	}
	if (state->filter_hidden)
		log_tag("filter:nohidden:\n");
	log_flush();
}

/**
 * Find a disk by name.
 */
static struct snapraid_disk* find_disk_by_name(struct snapraid_state* state, const char* name)
{
	tommy_node* i;

	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		if (strcmp(disk->name, name) == 0)
			return disk;
	}

	if (state->no_conf) {
		/* without a configuration file, add disks automatically */
		struct snapraid_disk* disk;

		disk = disk_alloc(name, "DUMMY/", -1, "", 0);

		tommy_list_insert_tail(&state->disklist, &disk->node, disk);

		return disk;
	}

	return 0;
}

/**
 * Find a disk by UUID.
 */
static struct snapraid_disk* find_disk_by_uuid(struct snapraid_state* state, const char* uuid)
{
	tommy_node* i;
	struct snapraid_disk* found = 0;

	/* special test case to find the first matching UUID */
	/* when testing UUID are all equal or not supported */
	/* and we should handle this case specifically */
	if (state->opt.match_first_uuid)
		return state->disklist->data;

	/* LCOV_EXCL_START */
	/* never find an empty uuid */
	if (!*uuid)
		return 0;

	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		if (strcmp(disk->uuid, uuid) == 0) {
			/* never match duplicate UUID */
			if (found)
				return 0;
			found = disk;
		}
	}

	return found;
	/* LCOV_EXCL_STOP */
}

/**
 * Update the disk mapping if required.
 */
static void state_map(struct snapraid_state* state)
{
	unsigned hole;
	tommy_node* i;
	unsigned uuid_mismatch;
	unsigned diskcount;
	unsigned l, s;

	/* remove all the mapping without a disk */
	/* this happens when a disk is removed from the configuration file */
	/* From SnapRAID 4.0 mappings are automatically removed if a disk is not used */
	/* when saving the content file, but we keep this code to import older content files. */
	for (i = state->maplist; i != 0; ) {
		struct snapraid_map* map = i->data;
		struct snapraid_disk* disk;

		disk = find_disk_by_name(state, map->name);

		/* go to the next mapping before removing */
		i = i->next;

		if (disk == 0) {
			/* disk not found, remove the mapping */
			tommy_list_remove_existing(&state->maplist, &map->node);
			map_free(map);
		}
	}

	/* maps each unmapped disk present in the configuration file in the first available hole */
	/* this happens when you add disks for the first time in the configuration file */
	hole = 0; /* first position to try */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_map* map;
		tommy_node* j;

		/* check if the disk is already mapped */
		for (j = state->maplist; j != 0; j = j->next) {
			map = j->data;
			if (strcmp(disk->name, map->name) == 0) {
				/* mapping found */
				break;
			}
		}
		if (j != 0) {
			/* mapping is present, then copy the free blocks into to disk */
			disk->total_blocks = map->total_blocks;
			disk->free_blocks = map->free_blocks;
			continue;
		}

		/* mapping not found, search for an hole */
		while (1) {
			for (j = state->maplist; j != 0; j = j->next) {
				map = j->data;
				if (map->position == hole) {
					/* position already used */
					break;
				}
			}
			if (j == 0) {
				/* hole found */
				break;
			}

			/* try with the next one */
			++hole;
		}

		/* insert the new mapping */
		map = map_alloc(disk->name, hole, 0, 0, "");

		tommy_list_insert_tail(&state->maplist, &map->node, map);
	}

	/* without configuration don't check for number of data disks or uuid changes */
	if (state->no_conf)
		return;

	/* counter for the number of UUID mismatches */
	uuid_mismatch = 0;

	/* check if mapping match the disk uuid */
	if (!state->opt.skip_disk_access) {
		for (i = state->maplist; i != 0; i = i->next) {
			struct snapraid_map* map = i->data;
			struct snapraid_disk* disk;

			disk = find_disk_by_name(state, map->name);
			if (disk == 0) {
				/* LCOV_EXCL_START */
				log_fatal("Internal inconsistency for mapping '%s'\n", map->name);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (disk->has_unsupported_uuid) {
				/* if uuid is not available, skip this one */
				continue;
			}

			/* if the uuid is changed */
			if (strcmp(disk->uuid, map->uuid) != 0) {
				/* mark the disk as with an UUID change */
				disk->has_different_uuid = 1;

				/* if the previous uuid is available */
				if (map->uuid[0] != 0) {
					/* count the number of uuid change */
					++uuid_mismatch;
					log_fatal("UUID change for disk '%s' from '%s' to '%s'\n", disk->name, map->uuid, disk->uuid);
				} else {
					/* no message here, because having a disk without */
					/* UUID is the normal state of an empty disk */
					disk->had_empty_uuid = 1;
				}

				/* update the uuid in the mapping, */
				pathcpy(map->uuid, sizeof(map->uuid), disk->uuid);

				/* write the new state with the new uuid */
				state->need_write = 1;
			}
		}
	}

	/* check the parity uuid */
	if (!state->opt.skip_parity_access) {
		for (l = 0; l < state->level; ++l) {
			for (s = 0; s < state->parity[l].split_mac; ++s) {
				char uuid[UUID_MAX];
				int ret;

				ret = devuuid(state->parity[l].split_map[s].device, uuid, sizeof(uuid));
				if (ret != 0) {
					/* uuid not available, just ignore */
					continue;
				}

				/* if the uuid is changed */
				if (strcmp(uuid, state->parity[l].split_map[s].uuid) != 0) {
					/* if the previous uuid is available */
					if (state->parity[l].split_map[s].uuid[0] != 0) {
						/* count the number of uuid change */
						++uuid_mismatch;
						log_fatal("UUID change for parity '%s[%u]' from '%s' to '%s'\n", lev_config_name(l), s, state->parity[l].split_map[s].uuid, uuid);
					}

					/* update the uuid */
					pathcpy(state->parity[l].split_map[s].uuid, sizeof(state->parity[l].split_map[s].uuid), uuid);

					/* write the new state with the new uuid */
					state->need_write = 1;
				}
			}
		}
	}

	if (!state->opt.force_uuid && uuid_mismatch > state->level) {
		/* LCOV_EXCL_START */
		log_fatal("Too many disks have UUID changed from the latest 'sync'.\n");
		log_fatal("If this happens because you really replaced them,\n");
		log_fatal("you can '%s' anyway, using 'snapraid --force-uuid %s'.\n", state->command, state->command);
		log_fatal("Instead, it's possible that you messed up the disk mount points,\n");
		log_fatal("and you have to restore the mount points at the state of the latest sync.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* count the number of data disks, including holes left after removing some */
	diskcount = 0;
	for (i = state->maplist; i != 0; i = i->next) {
		struct snapraid_map* map = i->data;

		if (map->position + 1 > diskcount)
			diskcount = map->position + 1;
	}

	/* ensure to don't go over the limit of the RAID engine */
	if (diskcount > RAID_DATA_MAX) {
		/* LCOV_EXCL_START */
		log_fatal("Too many data disks. No more than %u.\n", RAID_DATA_MAX);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* now count the real number of data disks, excluding holes left after removing some */
	diskcount = tommy_list_count(&state->maplist);

	/* recommend number of parities */
	if (!state->opt.no_warnings) {
		/* intentionally use log_fatal() instead of log_error() to give more visibility at the warning */
		if (diskcount >= 36 && state->level < 6) {
			log_fatal("WARNING! With %u disks it's recommended to use six parity levels.\n", diskcount);
		} else if (diskcount >= 29 && state->level < 5) {
			log_fatal("WARNING! With %u disks it's recommended to use five parity levels.\n", diskcount);
		} else if (diskcount >= 22 && state->level < 4) {
			log_fatal("WARNING! With %u disks it's recommended to use four parity levels.\n", diskcount);
		} else if (diskcount >= 15 && state->level < 3) {
			log_fatal("WARNING! With %u disks it's recommended to use three parity levels.\n", diskcount);
		} else if (diskcount >= 5 && state->level < 2) {
			log_fatal("WARNING! With %u disks it's recommended to use two parity levels.\n", diskcount);
		}
	}
}

void state_refresh(struct snapraid_state* state)
{
	tommy_node* i;
	unsigned l, s;

	/* for all disks */
	for (i = state->maplist; i != 0; i = i->next) {
		struct snapraid_map* map = i->data;
		struct snapraid_disk* disk;
		uint64_t total_space;
		uint64_t free_space;
		int ret;

		disk = find_disk_by_name(state, map->name);
		if (disk == 0) {
			/* LCOV_EXCL_START */
			log_fatal("Internal inconsistency for mapping '%s'\n", map->name);
			os_abort();
			/* LCOV_EXCL_STOP */
		}

		ret = fsinfo(disk->dir, 0, 0, &total_space, &free_space);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error accessing disk '%s' to get file-system info. %s.\n", disk->dir, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* set the new free blocks */
		map->total_blocks = total_space / state->block_size;
		map->free_blocks = free_space / state->block_size;

		/* also update the disk info */
		disk->total_blocks = map->total_blocks;
		disk->free_blocks = map->free_blocks;
	}

	/* for all parities */
	for (l = 0; l < state->level; ++l) {
		/* set the new free blocks */
		state->parity[l].total_blocks = 0;
		state->parity[l].free_blocks = 0;

		for (s = 0; s < state->parity[l].split_mac; ++s) {
			uint64_t total_space;
			uint64_t free_space;
			int ret;

			ret = fsinfo(state->parity[l].split_map[s].path, 0, 0, &total_space, &free_space);
			if (ret != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error accessing file '%s' to get file-system info. %s.\n", state->parity[l].split_map[s].path, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* add the new free blocks */
			state->parity[l].total_blocks += total_space / state->block_size;
			state->parity[l].free_blocks += free_space / state->block_size;
		}
	}

	/* note what we don't set need_write = 1, because we don't want */
	/* to update the content file only for the free space info. */
}

/**
 * Check the content.
 */
static void state_content_check(struct snapraid_state* state, const char* path)
{
	tommy_node* i;

	/* check that any map has different name and position */
	for (i = state->maplist; i != 0; i = i->next) {
		struct snapraid_map* map = i->data;
		tommy_node* j;
		for (j = i->next; j != 0; j = j->next) {
			struct snapraid_map* other = j->data;
			if (strcmp(map->name, other->name) == 0) {
				/* LCOV_EXCL_START */
				log_fatal("Colliding 'map' disk specification in '%s'\n", path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (map->position == other->position) {
				/* LCOV_EXCL_START */
				log_fatal("Colliding 'map' index specification in '%s'\n", path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}
}

/**
 * Check if the position is REQUIRED, or we can completely clear it from the state.
 *
 * Note that position with only DELETED blocks are discharged.
 */
static int fs_position_is_required(struct snapraid_state* state, block_off_t pos)
{
	tommy_node* i;

	/* check for each disk */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_block* block = fs_par2block_find(disk, pos);

		/* if we have at least one file, the position is needed */
		if (block_has_file(block))
			return 1;
	}

	return 0;
}

/**
 * Check if the info block is REQUIREQ.
 *
 * This is used to ensure that we keep the last check used for scrubbing.
 * and that we add it when importing old context files.
 *
 * Note that you can have position without info blocks, for example
 * if all the blocks are not synced.
 *
 * Note also that not requiring an info block, doesn't mean that if present it
 * can be discarded.
 */
static int fs_info_is_required(struct snapraid_state* state, block_off_t pos)
{
	tommy_node* i;

	/* check for each disk */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_block* block = fs_par2block_find(disk, pos);

		/* if we have at least one synced file, the info is required */
		if (block_state_get(block) == BLOCK_STATE_BLK)
			return 1;
	}

	return 0;
}

static void fs_position_clear_deleted(struct snapraid_state* state, block_off_t pos)
{
	tommy_node* i;

	/* check for each disk if block is really used */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_block* block = fs_par2block_find(disk, pos);

		/* if the block is deleted */
		if (block_state_get(block) == BLOCK_STATE_DELETED) {
			/* set it to empty */
			fs_deallocate(disk, pos);
		}
	}
}

/**
 * Check if a block position in a disk is deleted.
 */
static int fs_is_block_deleted(struct snapraid_disk* disk, block_off_t pos)
{
	struct snapraid_block* block = fs_par2block_find(disk, pos);

	return block_state_get(block) == BLOCK_STATE_DELETED;
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
		log_fatal("Unexpected end of content file '%s' at offset %" PRIi64 "\n", path, stell(f));
		log_fatal("This content file is truncated. Use an alternate copy.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (serror(f)) {
		/* LCOV_EXCL_START */
		log_fatal("Error reading the content file '%s' at offset %" PRIi64 "\n", path, stell(f));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	log_fatal("Decoding error in '%s' at offset %" PRIi64 "\n", path, stell(f));

	if (sdeplete(f, buf) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error flushing the content file '%s' at offset %" PRIi64 "\n", path, stell(f));
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
		log_fatal("Mismatching CRC in '%s'\n", path);
		log_fatal("This content file is damaged! Use an alternate copy.\n");
		exit(EXIT_FAILURE);
	} else {
		log_fatal("The file CRC is correct!\n");
	}
}

static void state_read_content(struct snapraid_state* state, const char* path, STREAM* f)
{
	block_off_t blockmax;
	unsigned count_file;
	unsigned count_hardlink;
	unsigned count_symlink;
	unsigned count_dir;
	int crc_checked;
	char buffer[PATH_MAX];
	int ret;
	tommy_array disk_mapping;
	uint32_t mapping_max;

	blockmax = 0;
	count_file = 0;
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
		log_fatal("Invalid header!\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}

	/*
	 * File format versions:
	 *  - SNAPCNT1/SnapRAID 4.0 First version.
	 *  - SNAPCNT2/SnapRAID 7.0 Adds entries 'M' and 'P', to add free_blocks support.
	 *    The previous 'm' entry is now deprecated, but supported for importing.
	 *    Similarly for text file, we add 'mapping' and 'parity' deprecating 'map'.
	 *  - SNAPCNT3/SnapRAID 11.0 Adds entry 'y' for hash size.
	 *  - SNAPCNT3/SnapRAID 11.0 Adds entry 'Q' for multi parity file.
	 *    The previous 'P' entry is now deprecated, but supported for importing.
	 */
	if (memcmp(buffer, "SNAPCNT1\n\3\0\0", 12) != 0
		&& memcmp(buffer, "SNAPCNT2\n\3\0\0", 12) != 0
		&& memcmp(buffer, "SNAPCNT3\n\3\0\0", 12) != 0
	) {
		/* LCOV_EXCL_START */
		if (memcmp(buffer, "SNAPCNT", 7) != 0) {
			decoding_error(path, f);
			log_fatal("Invalid header!\n");
			os_abort();
		} else {
			log_fatal("The content file '%s' was generated with a newer version of SnapRAID!\n", path);
			exit(EXIT_FAILURE);
		}
		/* LCOV_EXCL_STOP */
	}

	while (1) {
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
				log_fatal("Internal inconsistency in mapping index!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			ret = sgetb64(f, &v_size);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (state->block_size == 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Internal inconsistency due zero blocksize!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* check for impossible file size to avoid to crash for a too big allocation */
			if (v_size / state->block_size > blockmax) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Internal inconsistency in file size too big!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb64(f, &v_mtime_sec);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb32(f, &v_mtime_nsec);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
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
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetbs(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}
			if (!*sub) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Internal inconsistency for null file!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			/* allocate the file */
			file = file_alloc(state->block_size, sub, v_size, v_mtime_sec, v_mtime_nsec, v_inode, 0);

			/* insert the file in the file containers */
			tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));
			tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));
			tommy_hashdyn_insert(&disk->stampset, &file->stampset, file, file_stamp_hash(file->size, file->mtime_sec, file->mtime_nsec));
			tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);

			/* read all the blocks */
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
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				ret = sgetb32(f, &v_count);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				if (v_idx + v_count > file->blockmax) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					log_fatal("Internal inconsistency in block number!\n");
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				if (v_pos + v_count > blockmax) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					log_fatal("Internal inconsistency in block size %u/%u!\n", blockmax, v_pos + v_count);
					os_abort();
					/* LCOV_EXCL_START */
				}

				/* fill the blocks in the run */
				while (v_count) {
					struct snapraid_block* block = fs_file2block_get(file, v_idx);

					switch (c) {
					case 'b' :
						block_state_set(block, BLOCK_STATE_BLK);
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
					default :
						/* LCOV_EXCL_START */
						decoding_error(path, f);
						log_fatal("Invalid block type!\n");
						os_abort();
						/* LCOV_EXCL_STOP */
					}

					/* read the hash only for 'blk/chg/rep', and not for 'new' */
					if (c != 'n') {
						ret = sread(f, block->hash, BLOCK_HASH_SIZE);
						if (ret < 0) {
							/* LCOV_EXCL_START */
							decoding_error(path, f);
							os_abort();
							/* LCOV_EXCL_STOP */
						}
					} else {
						/* set the ZERO hash for deprecated NEW blocks */
						hash_zero_set(block->hash);
					}

					/* if the block contains a hash of past data */
					/* and we are clearing such indeterminate hashes */
					if (state->clear_past_hash
						&& block_has_past_hash(block)
					) {
						/* set the hash value to INVALID */
						hash_invalid_set(block->hash);
					}

					/* if we are disabling the copy optimization */
					/* we want also to clear any already previously stored information */
					/* in other sync commands */
					/* note that this is required only in sync, and we detect */
					/* this using the clear_past_hash flag */
					if (state->clear_past_hash
						&& state->opt.force_nocopy
						&& block_state_get(block) == BLOCK_STATE_REP
					) {
						/* set the hash value to INVALID */
						hash_invalid_set(block->hash);
						/* convert from REP to CHG block */
						block_state_set(block, BLOCK_STATE_CHG);
					}

					/* if we want a full reallocation, marks block as invalid parity */
					/* note that we do this after the force_nocopy option */
					/* to avoid to mixup the two things */
					if (state->opt.force_realloc
						&& block_state_get(block) == BLOCK_STATE_BLK) {
						/* convert from BLK to REP */
						block_state_set(block, BLOCK_STATE_REP);
					}

					/* set the parity association */
					fs_allocate(disk, v_pos, file, v_idx);

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
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			v_pos = 0;
			while (v_pos < blockmax) {
				int bad;
				int rehash;
				int justsynced;
				uint32_t t;
				uint32_t flag;
				uint32_t v_count;

				ret = sgetb32(f, &v_count);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				if (v_pos + v_count > blockmax) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					log_fatal("Internal inconsistency in info size %u/%u!\n", blockmax, v_pos + v_count);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				ret = sgetb32(f, &flag);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				/* if there is an info */
				if ((flag & 1) != 0) {
					/* read the time */
					ret = sgetb32(f, &t);
					if (ret < 0) {
						/* LCOV_EXCL_START */
						decoding_error(path, f);
						os_abort();
						/* LCOV_EXCL_STOP */
					}

					/* analyze the flags */
					bad = (flag & 2) != 0;
					rehash = (flag & 4) != 0;
					justsynced = (flag & 8) != 0;

					if (rehash && state->prevhash == HASH_UNDEFINED) {
						/* LCOV_EXCL_START */
						decoding_error(path, f);
						log_fatal("Internal inconsistency for missing previous checksum!\n");
						os_abort();
						/* LCOV_EXCL_STOP */
					}

					info = info_make(t + v_oldest, bad, rehash, justsynced);
				} else {
					info = 0;
				}

				while (v_count) {
					/* insert the info in the array */
					info_set(&state->infoarr, v_pos, info);

					/* ensure that an info is present only for used positions */
					if (fs_info_is_required(state, v_pos)) {
						if (!info) {
							/* LCOV_EXCL_START */
							decoding_error(path, f);
							log_fatal("Internal inconsistency for missing info!\n");
							os_abort();
							/* LCOV_EXCL_STOP */
						}
					} else {
						/* extra info are accepted for backward compatibility */
						/* they are discarded at the first write */
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
				log_fatal("Internal inconsistency in mapping index!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			v_pos = 0;
			while (v_pos < blockmax) {
				uint32_t v_idx;
				uint32_t v_count;
				struct snapraid_file* deleted;

				ret = sgetb32(f, &v_count);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				if (v_pos + v_count > blockmax) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					log_fatal("Internal inconsistency in hole size %u/%u!\n", blockmax, v_pos + v_count);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				/* get the sub-command */
				c = sgetc(f);

				switch (c) {
				case 'o' :
					/* if it's a run of deleted blocks */

					/* allocate a fake deleted file */
					deleted = file_alloc(state->block_size, "<deleted>", v_count * (data_off_t)state->block_size, 0, 0, 0, 0);

					/* mark the file as deleted */
					file_flag_set(deleted, FILE_IS_DELETED);

					/* insert it in the list of deleted files */
					tommy_list_insert_tail(&disk->deletedlist, &deleted->nodelist, deleted);

					/* process all blocks */
					v_idx = 0;
					while (v_count) {
						struct snapraid_block* block = fs_file2block_get(deleted, v_idx);

						/* set the block as deleted */
						block_state_set(block, BLOCK_STATE_DELETED);

						/* read the hash */
						ret = sread(f, block->hash, BLOCK_HASH_SIZE);
						if (ret < 0) {
							/* LCOV_EXCL_START */
							decoding_error(path, f);
							os_abort();
							/* LCOV_EXCL_STOP */
						}

						/* if we are clearing indeterminate hashes */
						if (state->clear_past_hash) {
							/* set the hash value to INVALID */
							hash_invalid_set(block->hash);
						}

						/* insert the block in the block array */
						fs_allocate(disk, v_pos, deleted, v_idx);

						/* go to next block */
						++v_pos;
						++v_idx;
						--v_count;
					}
					break;
				case 'O' :
					/* go to the next run */
					v_pos += v_count;
					break;
				default :
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					log_fatal("Invalid hole type!\n");
					os_abort();
					/* LCOV_EXCL_STOP */
				}
			}
		} else if (c == 's') {
			/* symlink */
			char sub[PATH_MAX];
			char linkto[PATH_MAX];
			struct snapraid_link* slink;
			struct snapraid_disk* disk;
			uint32_t mapping;

			ret = sgetb32(f, &mapping);
			if (ret < 0 || mapping >= mapping_max) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Internal inconsistency in mapping index!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			ret = sgetbs(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (!*sub) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Internal inconsistency for null symlink!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetbs(f, linkto, sizeof(linkto));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			/* allocate the link as symbolic link */
			slink = link_alloc(sub, linkto, FILE_IS_SYMLINK);

			/* insert the link in the link containers */
			tommy_hashdyn_insert(&disk->linkset, &slink->nodeset, slink, link_name_hash(slink->sub));
			tommy_list_insert_tail(&disk->linklist, &slink->nodelist, slink);

			/* stat */
			++count_symlink;
		} else if (c == 'a') {
			/* hardlink */
			char sub[PATH_MAX];
			char linkto[PATH_MAX];
			struct snapraid_link* slink;
			struct snapraid_disk* disk;
			uint32_t mapping;

			ret = sgetb32(f, &mapping);
			if (ret < 0 || mapping >= mapping_max) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Internal inconsistency in mapping index!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			ret = sgetbs(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (!*sub) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Internal inconsistency for null hardlink!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetbs(f, linkto, sizeof(linkto));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (!*linkto) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Internal inconsistency for empty hardlink '%s'!\n", sub);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			/* allocate the link as hard link */
			slink = link_alloc(sub, linkto, FILE_IS_HARDLINK);

			/* insert the link in the link containers */
			tommy_hashdyn_insert(&disk->linkset, &slink->nodeset, slink, link_name_hash(slink->sub));
			tommy_list_insert_tail(&disk->linklist, &slink->nodelist, slink);

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
				log_fatal("Internal inconsistency in mapping index!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}
			disk = tommy_array_get(&disk_mapping, mapping);

			ret = sgetbs(f, sub, sizeof(sub));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (!*sub) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Internal inconsistency for null dir!\n");
				os_abort();
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
			case 'm' :
				state->hash = HASH_METRO;
				break;
			default :
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Invalid checksum!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			/* read the seed */
			ret = sread(f, state->hashseed, HASH_MAX);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}
		} else if (c == 'C') {
			/* get the sub-command */
			c = sgetc(f);

			switch (c) {
			case 'u' :
				state->prevhash = HASH_MURMUR3;
				break;
			case 'k' :
				state->prevhash = HASH_SPOOKY2;
				break;
			case 'm' :
				state->prevhash = HASH_METRO;
				break;
			default :
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Invalid checksum!\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			/* read the seed */
			ret = sread(f, state->prevhashseed, HASH_MAX);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}
		} else if (c == 'z') {
			uint32_t block_size;

			ret = sgetb32(f, &block_size);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (block_size == 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Zero 'blocksize' specification in the content file!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* without configuration, auto assign the block size */
			if (state->no_conf) {
				state->block_size = block_size;
			}

			if (block_size != state->block_size) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Mismatching 'blocksize' specification in the content file!\n");
				log_fatal("Please restore the 'blocksize' value in the configuration file to '%u'\n", block_size / KIBI);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else if (c == 'y') {
			uint32_t hash_size;

			ret = sgetb32(f, &hash_size);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (hash_size < 2 || hash_size > HASH_MAX) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Invalid 'hashsize' specification in the content file!\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* without configuration, auto assign the block size */
			if (state->no_conf) {
				BLOCK_HASH_SIZE = hash_size;
			}

			if ((int)hash_size != BLOCK_HASH_SIZE) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Mismatching 'hashsize' specification in the content file!\n");
				log_fatal("Please restore the 'hashsize' value in the configuration file to '%u'\n", hash_size);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else if (c == 'x') {
			ret = sgetb32(f, &blockmax);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}
		} else if (c == 'm' || c == 'M') {
			struct snapraid_map* map;
			char uuid[UUID_MAX];
			uint32_t v_pos;
			uint32_t v_total_blocks;
			uint32_t v_free_blocks;
			struct snapraid_disk* disk;

			ret = sgetbs(f, buffer, sizeof(buffer));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb32(f, &v_pos);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			/* from SnapRAID 7.0 the 'M' command includes the free space */
			if (c == 'M') {
				ret = sgetb32(f, &v_total_blocks);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				ret = sgetb32(f, &v_free_blocks);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					os_abort();
					/* LCOV_EXCL_STOP */
				}
			} else {
				v_total_blocks = 0;
				v_free_blocks = 0;
			}

			/* read the uuid */
			ret = sgetbs(f, uuid, sizeof(uuid));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			/* find the disk */
			disk = find_disk_by_name(state, buffer);
			if (!disk) {
				/* search by UUID if renamed */
				disk = find_disk_by_uuid(state, uuid);
				if (disk) {
					log_fatal("WARNING! Renaming disk '%s' to '%s'\n", buffer, disk->name);

					/* write the new state with the new name */
					state->need_write = 1;
				}
			}
			if (!disk) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Disk '%s' with uuid '%s' not present in the configuration file!\n", buffer, uuid);
				log_fatal("If you have removed it from the configuration file, please restore it\n");
				/* if it's a command without UUID, it cannot autorename using UUID */
				if (state->opt.skip_disk_access)
					log_fatal("If you have renamed it, run 'sync' to update the new name\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			map = map_alloc(disk->name, v_pos, v_total_blocks, v_free_blocks, uuid);

			tommy_list_insert_tail(&state->maplist, &map->node, map);

			/* insert in the mapping vector */
			tommy_array_grow(&disk_mapping, mapping_max + 1);
			tommy_array_set(&disk_mapping, mapping_max, disk);
			++mapping_max;
		} else if (c == 'P') {
			/* from SnapRAID 7.0 the 'P' command includes the free space */
			/* from SnapRAID 11.0 the 'P' command is deprecated by 'Q' */
			char v_uuid[UUID_MAX];
			uint32_t v_level;
			uint32_t v_total_blocks;
			uint32_t v_free_blocks;

			ret = sgetb32(f, &v_level);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb32(f, &v_total_blocks);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb32(f, &v_free_blocks);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetbs(f, v_uuid, sizeof(v_uuid));
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (v_level >= LEV_MAX) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Invalid parity level '%u' in the configuration file!\n", v_level);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* auto configure if configuration is missing */
			if (state->no_conf) {
				if (v_level >= state->level)
					state->level = v_level + 1;
			}

			/* if we use this parity entry */
			if (v_level < state->level) {
				/* if the configuration has more splits, keep them */
				if (state->parity[v_level].split_mac < 1)
					state->parity[v_level].split_mac = 1;
				/* set the parity info */
				pathcpy(state->parity[v_level].split_map[0].uuid, sizeof(state->parity[v_level].split_map[0].uuid), v_uuid);
				state->parity[v_level].total_blocks = v_total_blocks;
				state->parity[v_level].free_blocks = v_free_blocks;
			}
		} else if (c == 'Q') {
			/* from SnapRAID 11.0 the 'Q' command include size info and multi file support  */
			uint32_t v_level;
			uint32_t v_total_blocks;
			uint32_t v_free_blocks;
			uint32_t v_split_mac;
			unsigned s;

			ret = sgetb32(f, &v_level);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb32(f, &v_total_blocks);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb32(f, &v_free_blocks);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			ret = sgetb32(f, &v_split_mac);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			if (v_level >= LEV_MAX) {
				/* LCOV_EXCL_START */
				decoding_error(path, f);
				log_fatal("Invalid parity level '%u' in the configuration file!\n", v_level);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* auto configure if configuration is missing */
			if (state->no_conf) {
				if (v_level >= state->level)
					state->level = v_level + 1;
				if (state->parity[v_level].split_mac < v_split_mac)
					state->parity[v_level].split_mac = v_split_mac;
			}

			/* if we use this parity entry */
			if (v_level < state->level) {
				/* set the parity info */
				state->parity[v_level].total_blocks = v_total_blocks;
				state->parity[v_level].free_blocks = v_free_blocks;
			}

			for (s = 0; s < v_split_mac; ++s) {
				char v_path[PATH_MAX];
				char v_uuid[UUID_MAX];
				uint64_t v_size;

				ret = sgetbs(f, v_path, sizeof(v_path));
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				ret = sgetbs(f, v_uuid, sizeof(v_uuid));
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				ret = sgetb64(f, &v_size);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					decoding_error(path, f);
					os_abort();
					/* LCOV_EXCL_STOP */
				}

				/* if we use this parity entry */
				if (v_level < state->level) {
					/* if this split was removed from the configuration */
					if (s >= state->parity[v_level].split_mac) {
						/* if the file is used, we really need it */
						if (v_size != 0) {
							/* LCOV_EXCL_START */
							decoding_error(path, f);
							log_fatal("Parity '%s' misses used file '%u'!\n", lev_config_name(v_level), s);
							log_fatal("If you have removed it from the configuration file, please restore it\n");
							exit(EXIT_FAILURE);
							/* LCOV_EXCL_STOP */
						}

						/* otherwise we can drop it */
						log_fatal("WARNING! Dropping from '%s' unused split '%u'\n", lev_config_name(v_level), s);
					} else {
						/* we copy the path only if without configuration file */
						if (state->no_conf)
							pathcpy(state->parity[v_level].split_map[s].path, sizeof(state->parity[v_level].split_map[s].path), v_path);

						/* set the split info */
						pathcpy(state->parity[v_level].split_map[s].uuid, sizeof(state->parity[v_level].split_map[s].uuid), v_uuid);
						state->parity[v_level].split_map[s].size = v_size;

						/* log the info read from the content file */
						log_tag("content:%s:%u:%s:%s:%" PRIi64 "\n", lev_config_name(v_level), s,
							state->parity[v_level].split_map[s].path,
							state->parity[v_level].split_map[s].uuid,
							state->parity[v_level].split_map[s].size);
					}
				}
			}
		} else if (c == 'N') {
			uint32_t crc_stored;
			uint32_t crc_computed;

			/* get the crc before reading it from the file */
			crc_computed = scrc(f);

			ret = sgetble32(f, &crc_stored);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				/* here don't call decoding_error() because it's too late to get the crc */
				log_fatal("Error reading the CRC in '%s' at offset %" PRIi64 "\n", path, stell(f));
				log_fatal("This content file is damaged! Use an alternate copy.\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (crc_stored != crc_computed) {
				/* LCOV_EXCL_START */
				/* here don't call decoding_error() because it's too late to get the crc */
				log_fatal("Mismatching CRC in '%s'\n", path);
				log_fatal("This content file is damaged! Use an alternate copy.\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			crc_checked = 1;
		} else {
			/* LCOV_EXCL_START */
			decoding_error(path, f);
			log_fatal("Invalid command '%c'!\n", (char)c);
			os_abort();
			/* LCOV_EXCL_STOP */
		}
	}

	tommy_array_done(&disk_mapping);

	if (serror(f)) {
		/* LCOV_EXCL_START */
		log_fatal("Error reading the content file '%s' at offset %" PRIi64 "\n", path, stell(f));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (!crc_checked) {
		/* LCOV_EXCL_START */
		log_fatal("Finished reading '%s' without finding the CRC\n", path);
		log_fatal("This content file is truncated or damaged! Use an alternate copy.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* check the file-system on all disks */
	state_fscheck(state, "after read");

	/* check that the stored parity size matches the loaded state */
	if (blockmax != parity_allocated_size(state)) {
		/* LCOV_EXCL_START */
		log_fatal("Internal inconsistency in parity size %u/%u in '%s' at offset %" PRIi64 "\n", blockmax, parity_allocated_size(state), path, stell(f));
		if (state->opt.skip_content_check) {
			log_fatal("Overriding.\n");
			blockmax = parity_allocated_size(state);
		} else {
			exit(EXIT_FAILURE);
		}
		/* LCOV_EXCL_STOP */
	}

	msg_verbose("%8u files\n", count_file);
	msg_verbose("%8u hardlinks\n", count_hardlink);
	msg_verbose("%8u symlinks\n", count_symlink);
	msg_verbose("%8u empty dirs\n", count_dir);
}

struct state_write_thread_context {
	struct snapraid_state* state;
#if HAVE_MT_WRITE
	thread_id_t thread;
#endif
	/* input */
	block_off_t blockmax;
	time_t info_oldest;
	time_t info_now;
	int info_has_rehash;
	STREAM* f;
	/* output */
	uint32_t crc;
	unsigned count_file;
	unsigned count_hardlink;
	unsigned count_symlink;
	unsigned count_dir;
};

static void* state_write_thread(void* arg)
{
	struct state_write_thread_context* context = arg;
	struct snapraid_state* state = context->state;
	block_off_t blockmax = context->blockmax;
	time_t info_oldest = context->info_oldest;
	time_t info_now = context->info_now;
	int info_has_rehash = context->info_has_rehash;
	STREAM* f = context->f;
	uint32_t crc;
	unsigned count_file;
	unsigned count_hardlink;
	unsigned count_symlink;
	unsigned count_dir;
	tommy_node* i;
	block_off_t idx;
	block_off_t begin;
	unsigned l, s;
	int version;

	count_file = 0;
	count_hardlink = 0;
	count_symlink = 0;
	count_dir = 0;

	/* check what version to use */
	version = 2;
	for (l = 0; l < state->level; ++l) {
		if (state->parity[l].split_mac > 1)
			version = 3;
	}
	if (BLOCK_HASH_SIZE != 16)
		version = 3;

	/* write header */
	if (version == 3)
		swrite("SNAPCNT3\n\3\0\0", 12, f);
	else
		swrite("SNAPCNT2\n\3\0\0", 12, f);

	/* write block size and block max */
	sputc('z', f);
	sputb32(state->block_size, f);
	sputc('x', f);
	sputb32(blockmax, f);

	/* hash size */
	if (version == 3) {
		sputc('y', f);
		sputb32(BLOCK_HASH_SIZE, f);
	}

	if (serror(f)) {
		/* LCOV_EXCL_START */
		log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		return context;
		/* LCOV_EXCL_STOP */
	}

	sputc('c', f);
	if (state->hash == HASH_MURMUR3) {
		sputc('u', f);
	} else if (state->hash == HASH_SPOOKY2) {
		sputc('k', f);
	} else if (state->hash == HASH_METRO) {
		sputc('m', f);
	} else {
		/* LCOV_EXCL_START */
		log_fatal("Unexpected hash when writing the content file '%s'.\n", serrorfile(f));
		return context;
		/* LCOV_EXCL_STOP */
	}
	swrite(state->hashseed, HASH_MAX, f);
	if (serror(f)) {
		/* LCOV_EXCL_START */
		log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		return context;
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
			} else if (state->prevhash == HASH_METRO) {
				sputc('m', f);
			} else {
				/* LCOV_EXCL_START */
				log_fatal("Unexpected prevhash when writing the content file '%s'.\n", serrorfile(f));
				return context;
				/* LCOV_EXCL_STOP */
			}
			swrite(state->prevhashseed, HASH_MAX, f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				return context;
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/* for each map */
	for (i = state->maplist; i != 0; i = i->next) {
		struct snapraid_map* map = i->data;
		struct snapraid_disk* disk;

		/* find the disk for this mapping */
		disk = find_disk_by_name(state, map->name);
		if (!disk) {
			/* LCOV_EXCL_START */
			log_fatal("Internal inconsistency for unmapped disk '%s'\n", map->name);
			return context;
			/* LCOV_EXCL_STOP */
		}

		/* save the mapping only if disk is mapped */
		if (disk->mapping_idx != -1) {
			sputc('M', f);
			sputbs(map->name, f);
			sputb32(map->position, f);
			sputb32(map->total_blocks, f);
			sputb32(map->free_blocks, f);
			sputbs(map->uuid, f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				return context;
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/* for each parity */
	for (l = 0; l < state->level; ++l) {
		if (version == 3) {
			sputc('Q', f);
			sputb32(l, f);
			sputb32(state->parity[l].total_blocks, f);
			sputb32(state->parity[l].free_blocks, f);
			sputb32(state->parity[l].split_mac, f);
			for (s = 0; s < state->parity[l].split_mac; ++s) {
				sputbs(state->parity[l].split_map[s].path, f);
				sputbs(state->parity[l].split_map[s].uuid, f);
				sputb64(state->parity[l].split_map[s].size, f);
			}
		} else {
			sputc('P', f);
			sputb32(l, f);
			sputb32(state->parity[l].total_blocks, f);
			sputb32(state->parity[l].free_blocks, f);
			sputbs(state->parity[l].split_map[0].uuid, f);
		}
		if (serror(f)) {
			/* LCOV_EXCL_START */
			log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
			return context;
			/* LCOV_EXCL_STOP */
		}
	}

	/* for each disk */
	for (i = state->disklist; i != 0; i = i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* if the disk is not mapped, skip it */
		if (disk->mapping_idx < 0)
			continue;

		/* for each file */
		for (j = disk->filelist; j != 0; j = j->next) {
			struct snapraid_file* file = j->data;
			uint64_t size;
			uint64_t mtime_sec;
			int32_t mtime_nsec;
			uint64_t inode;

			size = file->size;
			mtime_sec = file->mtime_sec;
			mtime_nsec = file->mtime_nsec;
			inode = file->inode;

			sputc('f', f);
			sputb32(disk->mapping_idx, f);
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
				log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				return context;
				/* LCOV_EXCL_STOP */
			}

			/* for all the blocks of the file */
			begin = 0;
			while (begin < file->blockmax) {
				unsigned v_state = block_state_get(fs_file2block_get(file, begin));
				block_off_t v_pos = fs_file2par_get(disk, file, begin);
				uint32_t v_count;

				block_off_t end;

				/* find the end of run of blocks */
				end = begin + 1;
				while (end < file->blockmax) {
					if (v_state != block_state_get(fs_file2block_get(file, end)))
						break;
					if (v_pos + (end - begin) != fs_file2par_get(disk, file, end))
						break;
					++end;
				}

				switch (v_state) {
				case BLOCK_STATE_BLK :
					sputc('b', f);
					break;
				case BLOCK_STATE_CHG :
					sputc('g', f);
					break;
				case BLOCK_STATE_REP :
					sputc('p', f);
					break;
				default :
					/* LCOV_EXCL_START */
					log_fatal("Internal inconsistency in state for block %u state %u\n", v_pos, v_state);
					return context;
					/* LCOV_EXCL_STOP */
				}

				sputb32(v_pos, f);

				v_count = end - begin;
				sputb32(v_count, f);

				/* write hashes */
				for (idx = begin; idx < end; ++idx) {
					struct snapraid_block* block = fs_file2block_get(file, idx);

					swrite(block->hash, BLOCK_HASH_SIZE, f);
				}

				if (serror(f)) {
					/* LCOV_EXCL_START */
					log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
					return context;
					/* LCOV_EXCL_STOP */
				}

				/* next begin position */
				begin = end;
			}

			++count_file;
		}

		/* for each link */
		for (j = disk->linklist; j != 0; j = j->next) {
			struct snapraid_link* slink = j->data;

			switch (link_flag_get(slink, FILE_IS_LINK_MASK)) {
			case FILE_IS_HARDLINK :
				sputc('a', f);
				++count_hardlink;
				break;
			case FILE_IS_SYMLINK :
				sputc('s', f);
				++count_symlink;
				break;
			}

			sputb32(disk->mapping_idx, f);
			sputbs(slink->sub, f);
			sputbs(slink->linkto, f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				return context;
				/* LCOV_EXCL_STOP */
			}
		}

		/* for each dir */
		for (j = disk->dirlist; j != 0; j = j->next) {
			struct snapraid_dir* dir = j->data;

			sputc('r', f);
			sputb32(disk->mapping_idx, f);
			sputbs(dir->sub, f);
			if (serror(f)) {
				/* LCOV_EXCL_START */
				log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				return context;
				/* LCOV_EXCL_STOP */
			}

			++count_dir;
		}

		/* deleted blocks of the disk */
		sputc('h', f);
		sputb32(disk->mapping_idx, f);
		if (serror(f)) {
			/* LCOV_EXCL_START */
			log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
			return context;
			/* LCOV_EXCL_STOP */
		}
		begin = 0;
		while (begin < blockmax) {
			int is_deleted;
			block_off_t end;

			is_deleted = fs_is_block_deleted(disk, begin);

			/* find the end of run of blocks */
			end = begin + 1;
			while (end < blockmax
				&& is_deleted == fs_is_block_deleted(disk, end)
			) {
				++end;
			}

			sputb32(end - begin, f);

			if (is_deleted) {
				/* write the run of deleted blocks with hash */
				sputc('o', f);

				/* write all the hash */
				while (begin < end) {
					struct snapraid_block* block = fs_par2block_get(disk, begin);

					swrite(block->hash, BLOCK_HASH_SIZE, f);

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
				log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				return context;
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
			&& info == info_get(&state->infoarr, end)
		) {
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
			if (info_get_justsynced(info))
				flag |= 8;
			sputb32(flag, f);

			t = info_get_time(info);

			/* truncate any time that is in the future */
			if (t > info_now)
				t = info_now;

			/* the oldest info is computed only on required blocks, so it may not be the absolute oldest */
			if (t < info_oldest)
				t = 0;
			else
				t -= info_oldest;

			sputb32(t, f);
		} else {
			/* write a special 0 flag to mark missing info */
			sputb32(0, f);
		}

		if (serror(f)) {
			/* LCOV_EXCL_START */
			log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
			return context;
			/* LCOV_EXCL_STOP */
		}

		/* next begin position */
		begin = end;
	}

	sputc('N', f);

	/* flush data written to the disk */
	if (sflush(f)) {
		/* LCOV_EXCL_START */
		log_fatal("Error writing the content file '%s' (in flush before crc). %s.\n", serrorfile(f), strerror(errno));
		return context;
		/* LCOV_EXCL_STOP */
	}

	/* get the file crc */
	crc = scrc(f);

	/* compare the crc of the data written to file */
	/* with the one of the data written to the stream */
	if (crc != scrc_stream(f)) {
		/* LCOV_EXCL_START */
		log_fatal("CRC mismatch writing the content stream.\n");
		log_fatal("DANGER! Your RAM memory is broken! DO NOT PROCEED UNTIL FIXED!\n");
		log_fatal("Try running a memory test like http://www.memtest86.com/\n");
		return context;
		/* LCOV_EXCL_STOP */
	}

	sputble32(crc, f);
	if (serror(f)) {
		/* LCOV_EXCL_START */
		log_fatal("Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		return context;
		/* LCOV_EXCL_STOP */
	}

	/* set output variables */
	context->crc = crc;
	context->count_file = count_file;
	context->count_hardlink = count_hardlink;
	context->count_symlink = count_symlink;
	context->count_dir = count_dir;

	return 0;
}

static void state_write_content(struct snapraid_state* state, uint32_t* out_crc)
{
#if HAVE_MT_WRITE
	int fail;
	int first;
#else
	STREAM* f;
	unsigned count_content;
	unsigned k;
	struct state_write_thread_context* context;
	void* retval;
#endif
	tommy_node* i;
	block_off_t blockmax;
	time_t info_oldest;
	time_t info_now;
	int info_has_rehash;
	int mapping_idx;
	block_off_t idx;
	uint32_t crc;
	unsigned count_file;
	unsigned count_hardlink;
	unsigned count_symlink;
	unsigned count_dir;

	/* blocks of all array */
	blockmax = parity_allocated_size(state);

	/* check the file-system on all disks */
	state_fscheck(state, "before write");

	/* clear the info for unused blocks */
	/* and get some other info */
	info_oldest = 0; /* oldest time in info */
	info_now = time(0); /* get the present time */
	info_has_rehash = 0; /* if there is a rehash info */
	for (idx = 0; idx < blockmax; ++idx) {
		/* if the position is used */
		if (fs_position_is_required(state, idx)) {
			snapraid_info info = info_get(&state->infoarr, idx);

			/* only if there is some info to store */
			if (info) {
				time_t info_time = info_get_time(info);

				if (!info_oldest || info_time < info_oldest)
					info_oldest = info_time;

				if (info_get_rehash(info))
					info_has_rehash = 1;
			}
		} else {
			/* clear any previous info */
			info_set(&state->infoarr, idx, 0);

			/* and clear any deleted blocks */
			fs_position_clear_deleted(state, idx);
		}
	}

	/* map disks */
	mapping_idx = 0;
	for (i = state->maplist; i != 0; i = i->next) {
		struct snapraid_map* map = i->data;
		struct snapraid_disk* disk;

		/* find the disk for this mapping */
		disk = find_disk_by_name(state, map->name);
		if (!disk) {
			/* LCOV_EXCL_START */
			log_fatal("Internal inconsistency for unmapped disk '%s'\n", map->name);
			os_abort();
			/* LCOV_EXCL_STOP */
		}

		/* save the mapping only for not empty disks */
		if (!fs_is_empty(disk, blockmax)) {
			/* assign the mapping index used to identify disks */
			disk->mapping_idx = mapping_idx;
			++mapping_idx;
		} else {
			/* mark the disk as without mapping */
			disk->mapping_idx = -1;
		}
	}

#if HAVE_MT_WRITE
	/* start all writing threads */
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		struct state_write_thread_context* context;
		char tmp[PATH_MAX];
		STREAM* f;

		msg_progress("Saving state to %s...\n", content->content);

		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);

		/* ensure to delete a previous stale file */
		if (remove(tmp) != 0) {
			if (errno != ENOENT) {
				/* LCOV_EXCL_START */
				log_fatal("Error removing the stale content file '%s'. %s.\n", tmp, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		f = sopen_write(tmp);
		if (f == 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error opening the temporary content file '%s'. %s.\n", tmp, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* allocate the thread context */
		context = malloc_nofail(sizeof(struct state_write_thread_context));
		content->context = context;

		/* initialize */
		context->state = state;
		context->blockmax = blockmax;
		context->info_oldest = info_oldest;
		context->info_now = info_now;
		context->info_has_rehash = info_has_rehash;
		context->f = f;

		thread_create(&context->thread, state_write_thread, context);

		i = i->next;
	}

	/* join all thread */
	fail = 0;
	first = 1;
	crc = 0;
	count_file = 0;
	count_hardlink = 0;
	count_symlink = 0;
	count_dir = 0;
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		struct state_write_thread_context* context = content->context;
		void* retval;

		thread_join(context->thread, &retval);

		if (retval) {
			/* LCOV_EXCL_START */
			fail = 1;
			/* LCOV_EXCL_STOP */
		} else {
			STREAM* f = context->f;

			/* Use the sequence fflush() -> fsync() -> fclose() -> rename() to ensure */
			/* than even in a system crash event we have one valid copy of the file. */
			if (sflush(f) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error writing the content file '%s', in flush(). %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

#if HAVE_FSYNC
			if (ssync(f) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error writing the content file '%s' in sync(). %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
#endif

			if (sclose(f) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error closing the content file. %s.\n", strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (first) {
				first = 0;
				crc = context->crc;
				count_file = context->count_file;
				count_hardlink = context->count_hardlink;
				count_symlink = context->count_symlink;
				count_dir = context->count_dir;
			} else {
				if (crc != context->crc) {
					/* LCOV_EXCL_START */
					log_fatal("Different CRCs writing content streams.\n");
					log_fatal("DANGER! Your RAM memory is broken! DO NOT PROCEED UNTIL FIXED!\n");
					log_fatal("Try running a memory test like http://www.memtest86.com/\n");
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
		}

		free(context);

		i = i->next;
	}

	/* abort on failure */
	if (fail) {
		/* LCOV_EXCL_START */
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
#else
	/* count the content files */
	count_content = 0;
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		msg_progress("Saving state to %s...\n", content->content);
		++count_content;
		i = i->next;
	}

	/* open all the content files */
	f = sopen_multi_write(count_content);
	if (!f) {
		/* LCOV_EXCL_START */
		log_fatal("Error opening the content files.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	k = 0;
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		char tmp[PATH_MAX];
		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);

		/* ensure to delete a previous stale file */
		if (remove(tmp) != 0) {
			if (errno != ENOENT) {
				/* LCOV_EXCL_START */
				log_fatal("Error removing the stale content file '%s'. %s.\n", tmp, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		if (sopen_multi_file(f, k, tmp) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error opening the temporary content file '%s'. %s.\n", tmp, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		++k;
		i = i->next;
	}

	/* allocate the thread context */
	context = malloc_nofail(sizeof(struct state_write_thread_context));

	/* initialize */
	context->state = state;
	context->blockmax = blockmax;
	context->info_oldest = info_oldest;
	context->info_now = info_now;
	context->info_has_rehash = info_has_rehash;
	context->f = f;

	retval = state_write_thread(context);

	/* abort on failure */
	if (retval) {
		/* LCOV_EXCL_START */
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* Use the sequence fflush() -> fsync() -> fclose() -> rename() to ensure */
	/* than even in a system crash event we have one valid copy of the file. */
	if (sflush(f) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error writing the content file '%s', in flush(). %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

#if HAVE_FSYNC
	if (ssync(f) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error writing the content file '%s' in sync(). %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
#endif

	if (sclose(f) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error closing the content file. %s.\n", strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	crc = context->crc;
	count_file = context->count_file;
	count_hardlink = context->count_hardlink;
	count_symlink = context->count_symlink;
	count_dir = context->count_dir;

	free(context);
#endif

	msg_verbose("%8u files\n", count_file);
	msg_verbose("%8u hardlinks\n", count_hardlink);
	msg_verbose("%8u symlinks\n", count_symlink);
	msg_verbose("%8u empty dirs\n", count_dir);

	*out_crc = crc;
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
			log_tag("content:%s\n", path);
			log_flush();
		}
		msg_progress("Loading state from %s...\n", path);

		f = sopen_read(path);
		if (f != 0) {
			/* if opened stop the search */
			break;
		} else {
			/* if it's real error of an existing file, abort */
			if (errno != ENOENT) {
				/* LCOV_EXCL_START */
				log_fatal("Error opening the content file '%s'. %s.\n", path, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* otherwise continue */
			if (node->next) {
				log_fatal("WARNING! Content file '%s' not found, trying with another copy...\n", path);

				/* ensure to rewrite all the content files */
				state->need_write = 1;
			}
		}

		/* next content file */
		node = node->next;
	}

	/* if not found, assume empty */
	if (!f) {
		log_fatal("No content file found. Assuming empty.\n");

		/* create the initial mapping */
		state_map(state);
		return;
	}

	/* get the stat of the content file */
	ret = fstat(shandle(f), &st);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error stating the content file '%s'. %s.\n", path, strerror(errno));
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
				log_fatal("Error stating the content file '%s'. %s.\n", other_path, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			/* ensure to rewrite all the content files */
			state->need_write = 1;
		} else {
			/* if the size is different */
			if (other_st.st_size != st.st_size) {
				log_fatal("WARNING! Content files '%s' and '%s' have a different size!\n", path, other_path);
				log_fatal("Likely one of the two is broken!\n");

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
	memset(state->hashseed, 0, HASH_MAX);

	/* previous hash, start with an undefined value */
	state->prevhash = HASH_UNDEFINED;

	/* intentionally not set the prevhashseed, if used valgrind will warn about it */

	/* get the first char to detect the file type */
	c = sgetc(f);
	sungetc(c, f);

	/* guess the file type from the first char */
	if (c == 'S') {
		state_read_content(state, path, f);
	} else {
		/* LCOV_EXCL_START */
		log_fatal("From SnapRAID v9.0 the text content file is not supported anymore.\n");
		log_fatal("You have first to upgrade to SnapRAID v8.1 to convert it to binary format.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	sclose(f);

	if (state->hash == HASH_UNDEFINED) {
		/* LCOV_EXCL_START */
		log_fatal("The checksum to use is not specified.\n");
		log_fatal("This happens because you are likely upgrading from SnapRAID 1.0.\n");
		log_fatal("To use a new SnapRAID you must restart from scratch,\n");
		log_fatal("deleting all the content and parity files.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* update the mapping */
	state_map(state);

	state_content_check(state, path);

	/* mark that we read the content file, and it passed all the checks */
	state->checked_read = 1;
}

struct state_verify_thread_context {
	struct snapraid_state* state;
	struct snapraid_content* content;
#if HAVE_MT_VERIFY
	thread_id_t thread;
#else
	void* retval;
#endif
	/* input */
	uint32_t crc;
	STREAM* f;
};

static void* state_verify_thread(void* arg)
{
	struct state_verify_thread_context* context = arg;
	struct snapraid_content* content = context->content;
	STREAM* f = context->f;
	unsigned char buf[4];
	uint32_t crc_stored;
	uint32_t crc_computed;
	uint64_t start;

	start = tick_ms();

	if (sdeplete(f, buf) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error flushing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		return context;
		/* LCOV_EXCL_STOP */
	}

	/* get the stored crc from the last four bytes */
	crc_stored = buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;

	if (crc_stored != context->crc) {
		/* LCOV_EXCL_START */
		log_fatal("DANGER! Wrong stored CRC in '%s'\n", serrorfile(f));
		return context;
		/* LCOV_EXCL_STOP */
	}

	/* get the computed crc */
	crc_computed = scrc(f);

	/* adjust the stored crc to include itself */
	crc_stored = crc32c(crc_stored, buf, 4);

	if (crc_computed != crc_stored) {
		/* LCOV_EXCL_START */
		log_fatal("DANGER! Wrong file CRC in '%s'\n", serrorfile(f));
		return context;
		/* LCOV_EXCL_STOP */
	}

	msg_progress("Verified %s in %" PRIu64 " seconds\n", content->content, (tick_ms() - start) / 1000);

	return 0;
}

static void state_verify_content(struct snapraid_state* state, uint32_t crc)
{
	tommy_node* i;
	int fail;

	msg_progress("Verifying...\n");

	/* start all reading threads */
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		struct state_verify_thread_context* context;
		char tmp[PATH_MAX];
		STREAM* f;

		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);
		f = sopen_read(tmp);
		if (f == 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error reopening the temporary content file '%s'. %s.\n", tmp, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* allocate the thread context */
		context = malloc_nofail(sizeof(struct state_verify_thread_context));
		content->context = context;

		/* initialize */
		context->state = state;
		context->content = content;
		context->crc = crc;
		context->f = f;

#if HAVE_MT_VERIFY
		thread_create(&context->thread, state_verify_thread, context);
#else
		context->retval = state_verify_thread(context);
#endif

		i = i->next;
	}

	/* join all thread */
	fail = 0;
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		struct state_verify_thread_context* context = content->context;
		void* retval;

#if HAVE_MT_VERIFY
		thread_join(context->thread, &retval);
#else
		retval = context->retval;
#endif
		if (retval) {
			/* LCOV_EXCL_START */
			fail = 1;
			/* LCOV_EXCL_STOP */
		} else {
			STREAM* f = context->f;

			if (sclose(f) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error closing the content file. %s.\n", strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		free(context);

		i = i->next;
	}

	/* abort on failure */
	if (fail) {
		/* LCOV_EXCL_START */
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

static void state_rename_content(struct snapraid_state* state)
{
	tommy_node* i;

#if defined(_linux) /* this sequence is linux specific */
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		char tmp[PATH_MAX];
		char dir[PATH_MAX];
		char* slash;
		int handle;

		pathcpy(dir, sizeof(dir), content->content);

		slash = strrchr(tmp, '/');
		if (slash)
			*slash = 0;
		else
			pathcpy(dir, sizeof(dir), ".");

		/* open the directory to get the handle */
		handle = open(dir, O_RDONLY | O_DIRECTORY);
		if (handle < 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error opening the directory '%s'. %s.\n", dir, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* now rename the just written copy with the correct name */
		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);
		if (rename(tmp, content->content) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error renaming the content file '%s' to '%s'. %s.\n", tmp, content->content, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* sync the directory */
		if (fsync(handle) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error syncing the directory '%s'. %s.\n", dir, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (close(handle) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error closing the directory '%s'. %s.\n", dir, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		i = i->next;
	}
#else
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		char tmp[PATH_MAX];

		/* now renames the just written copy with the correct name */
		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);
		if (rename(tmp, content->content) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error renaming the content file '%s' to '%s' in rename(). %s.\n", tmp, content->content, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		i = i->next;
	}
#endif
}

void state_write(struct snapraid_state* state)
{
	uint32_t crc;

	/* write all the content files */
	state_write_content(state, &crc);

	/* verify the just written files */
	state_verify_content(state, crc);

	/* rename the new files, over the old ones */
	state_rename_content(state);

	state->need_write = 0; /* no write needed anymore */
	state->checked_read = 0; /* what we wrote is not checked in read */
}

void state_skip(struct snapraid_state* state)
{
	tommy_node* i;

	/* for each disk */
	for (i = state->disklist; i != 0; i = i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		if (!disk->skip_access)
			continue;

		/* for each file */
		for (j = tommy_list_head(&disk->filelist); j != 0; j = j->next) {
			struct snapraid_file* file = j->data;
			file_flag_set(file, FILE_IS_EXCLUDED);
		}

		/* for each link */
		for (j = tommy_list_head(&disk->linklist); j != 0; j = j->next) {
			struct snapraid_link* slink = j->data;
			link_flag_set(slink, FILE_IS_EXCLUDED);
		}

		/* for each dir */
		for (j = tommy_list_head(&disk->dirlist); j != 0; j = j->next) {
			struct snapraid_dir* dir = j->data;
			dir_flag_set(dir, FILE_IS_EXCLUDED);
		}
	}
}

void state_filter(struct snapraid_state* state, tommy_list* filterlist_file, tommy_list* filterlist_disk, int filter_missing, int filter_error)
{
	tommy_node* i;
	unsigned l;

	/* if no filter, include all */
	if (!filter_missing && !filter_error && tommy_list_empty(filterlist_file) && tommy_list_empty(filterlist_disk))
		return;

	msg_progress("Selecting...\n");

	for (i = tommy_list_head(filterlist_disk); i != 0; i = i->next) {
		struct snapraid_filter* filter = i->data;
		msg_verbose("\t%s%s\n", filter->pattern, filter->is_disk ? "//" : "");
	}
	for (i = tommy_list_head(filterlist_file); i != 0; i = i->next) {
		struct snapraid_filter* filter = i->data;
		msg_verbose("\t%s%s\n", filter->pattern, filter->is_dir ? "/" : "");
	}
	if (filter_missing)
		msg_verbose("\t<missing>\n");
	if (filter_error)
		msg_verbose("\t<error>\n");

	/* for each disk */
	for (i = state->disklist; i != 0; i = i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* if we filter for presence, we have to access the disk, so better to print something */
		if (filter_missing)
			msg_progress("Scanning disk %s...\n", disk->name);

		/* for each file */
		for (j = tommy_list_head(&disk->filelist); j != 0; j = j->next) {
			struct snapraid_file* file = j->data;

			if (filter_path(filterlist_disk, 0, disk->name, file->sub) != 0
				|| filter_path(filterlist_file, 0, disk->name, file->sub) != 0
				|| filter_existence(filter_missing, disk->dir, file->sub) != 0
				|| filter_correctness(filter_error, &state->infoarr, disk, file) != 0
			) {
				file_flag_set(file, FILE_IS_EXCLUDED);
			}
		}

		/* for each link */
		for (j = tommy_list_head(&disk->linklist); j != 0; j = j->next) {
			struct snapraid_link* slink = j->data;

			if (filter_path(filterlist_disk, 0, disk->name, slink->sub) != 0
				|| filter_path(filterlist_file, 0, disk->name, slink->sub) != 0
				|| filter_existence(filter_missing, disk->dir, slink->sub) != 0
			) {
				link_flag_set(slink, FILE_IS_EXCLUDED);
			}
		}

		/* for each empty dir */
		for (j = tommy_list_head(&disk->dirlist); j != 0; j = j->next) {
			struct snapraid_dir* dir = j->data;

			if (filter_emptydir(filterlist_disk, 0, disk->name, dir->sub) != 0
				|| filter_emptydir(filterlist_file, 0, disk->name, dir->sub) != 0
				|| filter_existence(filter_missing, disk->dir, dir->sub) != 0
			) {
				dir_flag_set(dir, FILE_IS_EXCLUDED);
			}
		}
	}

	/* if we are filtering by disk, exclude any parity not explicitly included */
	if (!tommy_list_empty(filterlist_disk)) {
		/* for each parity disk */
		for (l = 0; l < state->level; ++l) {
			/* check if the parity is excluded by name */
			if (filter_path(filterlist_disk, 0, lev_config_name(l), 0) != 0) {
				/* excluded the parity from further operation */
				state->parity[l].is_excluded_by_filter = 1;
			}
		}
	} else {
		/* if we are filtering by file, exclude all parity */
		if (filter_missing || !tommy_list_empty(filterlist_file)) {
			/* for each parity disk */
			for (l = 0; l < state->level; ++l) {
				state->parity[l].is_excluded_by_filter = 1;
			}
		}
	}
}

int state_progress_begin(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax, block_off_t countmax)
{
	time_t now;

	if (state->opt.gui) {
		log_tag("run:begin:%u:%u:%u\n", blockstart, blockmax, countmax);
		log_flush();
	}

	now = time(0);

	state->progress_whole_start = now;

	state->progress_tick = 0;
	state->progress_ptr = 0;
	state->progress_wasted = 0;

	/* stop if requested */
	if (global_interrupt) {
		/* LCOV_EXCL_START */
		if (!state->opt.gui) {
			msg_status("Not starting for interruption\n");
		}
		log_tag("sigint:0: SIGINT received\n");
		log_flush();
		return 0;
		/* LCOV_EXCL_STOP */
	}

	return 1;
}

void state_progress_end(struct snapraid_state* state, block_off_t countpos, block_off_t countmax, data_off_t countsize)
{
	if (state->opt.gui) {
		log_tag("run:end\n");
		log_flush();
	} else if (countmax == 0) {
		msg_status("Nothing to do\n");
	} else {
		time_t now;
		time_t elapsed;

		unsigned countsize_MB = (countsize + MEGA - 1) / MEGA;

		now = time(0);

		elapsed = now - state->progress_whole_start - state->progress_wasted;

		msg_bar("%u%% completed, %u MB accessed", countpos * 100 / countmax, countsize_MB);

		msg_bar(" in %u:%02u", (unsigned)(elapsed / 3600), (unsigned)((elapsed % 3600) / 60));

		/* some extra spaces to ensure to overwrite the latest status line */
		msg_bar("    ");

		msg_bar("\n");
		msg_flush();
	}
}

void state_progress_stop(struct snapraid_state* state)
{
	time_t now;

	now = time(0);

	if (!state->opt.gui) {
		msg_bar("\n");
		msg_flush();
	}

	state->progress_interruption = now;
}

void state_progress_restart(struct snapraid_state* state)
{
	time_t now;

	now = time(0);

	/* reset the progress counter */
	state->progress_tick = 0;
	state->progress_ptr = 0;

	if (now >= state->progress_interruption) /* avoid degenerated cases when the clock is manually adjusted */
		state->progress_wasted += now - state->progress_interruption;
}

#define PROGRESS_CLEAR "          "

/**
 * Set the latest tick data in the progress vector.
 */
static void state_progress_latest(struct snapraid_state* state)
{
	tommy_node* i;
	unsigned l;

	state->progress_tick_misc[state->progress_ptr] = state->tick_misc;
	state->progress_tick_sched[state->progress_ptr] = state->tick_sched;
	state->progress_tick_raid[state->progress_ptr] = state->tick_raid;
	state->progress_tick_hash[state->progress_ptr] = state->tick_hash;
	state->progress_tick_io[state->progress_ptr] = state->tick_io;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		disk->progress_tick[state->progress_ptr] = disk->tick;
	}
	for (l = 0; l < state->level; ++l)
		state->parity[l].progress_tick[state->progress_ptr] = state->parity[l].tick;
}

/**
 * Number of columns
 */
#define GRAPH_COLUMN 68

/**
 * Get the reference value, 0 if index is PROGRESS_MAX
 */
#define ref(map, index) (index < PROGRESS_MAX ? map[index] : 0)

static void state_progress_graph(struct snapraid_state* state, struct snapraid_io* io, unsigned current, unsigned oldest)
{
	uint64_t v;
	uint64_t tick_total;
	unsigned bar;
	tommy_node* i;
	unsigned l;
	size_t pad;

	tick_total = 0;

	tick_total += state->progress_tick_misc[current] - ref(state->progress_tick_misc, oldest);
	tick_total += state->progress_tick_sched[current] - ref(state->progress_tick_sched, oldest);
	tick_total += state->progress_tick_raid[current] - ref(state->progress_tick_raid, oldest);
	tick_total += state->progress_tick_hash[current] - ref(state->progress_tick_hash, oldest);
	tick_total += state->progress_tick_io[current] - ref(state->progress_tick_io, oldest);
	if (!tick_total)
		return;

	pad = 4;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		size_t len = strlen(disk->name);
		if (pad < len)
			pad = len;
	}
	for (l = 0; l < state->level; ++l) {
		size_t len = strlen(lev_config_name(l));
		if (pad < len)
			pad = len;
	}

	/* extra space */
	pad += 1;

	if (pad + 30 < GRAPH_COLUMN)
		bar = GRAPH_COLUMN - pad;
	else
		bar = 30;

	if (io) {
		const char* legend = "cached blocks (instant, more is better)";

		/* refresh the cached blocks info */
		io_refresh(io);

		printf("\n");

		/* search for the slowest */
		for (i = state->disklist; i != 0; i = i->next) {
			struct snapraid_disk* disk = i->data;
			v = disk->cached_blocks;
			printr(disk->name, pad);
			printf("%4" PRIu64 " | ", v);

			if (disk->progress_file && disk->progress_file->sub)
				printf("%s", disk->progress_file->sub);
			else
				printf("-");

			printf("\n");
		}

		for (l = 0; l < state->level; ++l) {
			v = state->parity[l].cached_blocks;
			printr(lev_config_name(l), pad);
			printf("%4" PRIu64 " | ", v);
			printc('o', v * bar / io->io_max);
			printf("\n");
		}

		printc(' ', pad);
		printf("     |_");
		printc('_', bar);
		printf("\n");

		printc(' ', 5 + pad + 1 + bar / 2 - strlen(legend) / 2);
		printf("%s", legend);
		printf("\n");
	}

	printf("\n");

	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		v = disk->progress_tick[current] - ref(disk->progress_tick, oldest);
		printr(disk->name, pad);
		printf("%3" PRIu64 "%% | ", v * 100 / tick_total);
		printc('*', v * bar / tick_total);
		printf("\n");

		/* clear the file in progress */
		disk->progress_file = 0;
	}

	for (l = 0; l < state->level; ++l) {
		v = state->parity[l].progress_tick[current] - ref(state->parity[l].progress_tick, oldest);
		printr(lev_config_name(l), pad);
		printf("%3" PRIu64 "%% | ", v * 100 / tick_total);
		printc('*', v * bar / tick_total);
		printf("\n");
	}

	v = state->progress_tick_raid[current] - ref(state->progress_tick_raid, oldest);
	printr("raid", pad);
	printf("%3" PRIu64 "%% | ", v * 100 / tick_total);
	printc('*', v * bar / tick_total);
	printf("\n");

	v = state->progress_tick_hash[current] - ref(state->progress_tick_hash, oldest);
	printr("hash", pad);
	printf("%3" PRIu64 "%% | ", v * 100 / tick_total);
	printc('*', v * bar / tick_total);
	printf("\n");

	v = state->progress_tick_sched[current] - ref(state->progress_tick_sched, oldest);
	printr("sched", pad);
	printf("%3" PRIu64 "%% | ", v * 100 / tick_total);
	printc('*', v * bar / tick_total);
	printf("\n");

	v = state->progress_tick_misc[current] - ref(state->progress_tick_misc, oldest);
	printr("misc", pad);
	printf("%3" PRIu64 "%% | ", v * 100 / tick_total);
	printc('*', v * bar / tick_total);
	printf("\n");

	printc(' ', pad);
	printf("     |_");
	printc('_', bar);
	printf("\n");

	if (oldest == PROGRESS_MAX) {
		const char* legend = "wait time (total, less is better)";
		printc(' ', 5 + pad + 1 + bar / 2 - strlen(legend) / 2);
		printf("%s", legend);
		printf("\n");
	} else {
		const char* legend_d = "wait time (last %d secs, less is better)";
		printc(' ', 5 + pad + 1 + bar / 2 - strlen(legend_d) / 2);
		printf(legend_d, PROGRESS_MAX);
		printf("\n");
	}

	printf("\n");
}

int state_progress(struct snapraid_state* state, struct snapraid_io* io, block_off_t blockpos, block_off_t countpos, block_off_t countmax, data_off_t countsize)
{
	time_t now;
	int pred;

	now = time(0);

	/* previous position */
	pred = state->progress_ptr + PROGRESS_MAX - 1;
	if (pred >= PROGRESS_MAX)
		pred -= PROGRESS_MAX;

	/* if the previous measure is different */
	if (state->progress_tick == 0
		|| state->progress_time[pred] != now
	) {
		time_t elapsed;
		unsigned out_perc = 0;
		unsigned out_size_speed = 0;
		unsigned out_block_speed = 0;
		unsigned out_cpu = 0;
		unsigned out_eta = 0;
		int out_computed = 0;

		/* store the new measure */
		state->progress_time[state->progress_ptr] = now;
		state->progress_pos[state->progress_ptr] = countpos;
		state->progress_size[state->progress_ptr] = countsize;
		state_progress_latest(state);

		elapsed = now - state->progress_whole_start - state->progress_wasted;

		/* completion percentage */
		if (countmax)
			out_perc = countpos * 100 / countmax;

		/* if we have at least 5 measures */
		if (state->progress_tick >= 5
		        /* or if we are running in test mode, with at least one measure */
			|| (state->opt.force_progress && state->progress_tick >= 1)
		) {
			int oldest;
			int past;
			time_t delta_time;
			block_off_t delta_pos;
			data_off_t delta_size;
			uint64_t tick_cpu;
			uint64_t tick_total;
			uint64_t delta_tick_cpu;
			uint64_t delta_tick_total;
			uint64_t oldest_tick_cpu;
			uint64_t oldest_tick_total;

			/* number of past measures */
			past = state->progress_tick;

			/* drop the oldest ones, to promptly */
			/* skip the startup phase */
			past -= past / 5;

			/* check how much we can go in the past */
			if (past >= PROGRESS_MAX - 1) {
				/* the vector is filled, so we are already in position */
				/* to get the possible oldest one */
				oldest = state->progress_ptr + 1;
			} else {
				/* go backward the number of positions selected */
				oldest = state->progress_ptr + PROGRESS_MAX - past;
			}
			if (oldest >= PROGRESS_MAX)
				oldest -= PROGRESS_MAX;

			tick_cpu = state->progress_tick_misc[state->progress_ptr]
				+ state->progress_tick_sched[state->progress_ptr]
				+ state->progress_tick_raid[state->progress_ptr]
				+ state->progress_tick_hash[state->progress_ptr];
			tick_total = tick_cpu + state->progress_tick_io[state->progress_ptr];

			oldest_tick_cpu = state->progress_tick_misc[oldest]
				+ state->progress_tick_sched[oldest]
				+ state->progress_tick_raid[oldest]
				+ state->progress_tick_hash[oldest];
			oldest_tick_total = oldest_tick_cpu + state->progress_tick_io[oldest];

			delta_time = now - state->progress_time[oldest];
			delta_pos = countpos - state->progress_pos[oldest];
			delta_size = countsize - state->progress_size[oldest];
			delta_tick_cpu = tick_cpu - oldest_tick_cpu;
			delta_tick_total = tick_total - oldest_tick_total;

			/* estimate the speed in MB/s */
			if (delta_time != 0)
				out_size_speed = (unsigned)(delta_size / MEGA / delta_time);

			/* estimate the speed in block/s */
			if (delta_time != 0)
				out_block_speed = (unsigned)(delta_pos / delta_time);

			/* estimate the cpu usage percentage */
			if (delta_tick_total != 0)
				out_cpu = (unsigned)(delta_tick_cpu * 100U / delta_tick_total);

			/* estimate the remaining time in minutes */
			if (delta_pos != 0)
				out_eta = (countmax - countpos) * delta_time / (60 * delta_pos);

			if (state->opt.force_stats) {
				os_clear();
				state_progress_graph(state, io, state->progress_ptr, oldest);
			}

			/* we have the output value */
			out_computed = 1;
		}

		if (state->opt.gui) {
			log_tag("run:pos:%u:%u:%" PRIu64 ":%u:%u:%u:%u:%" PRIu64 "\n", blockpos, countpos, countsize, out_perc, out_eta, out_size_speed, out_cpu, (uint64_t)elapsed);
			log_flush();
		} else {
			msg_bar("%u%%, %u MB", out_perc, (unsigned)(countsize / MEGA));
			if (out_computed) {
				msg_bar(", %u MB/s", out_size_speed);
				msg_bar(", %u stripe/s", out_block_speed);
				msg_bar(", CPU %u%%", out_cpu);
				msg_bar(", %u:%02u ETA", out_eta / 60, out_eta % 60);
			}
			msg_bar("%s\r", PROGRESS_CLEAR);
			msg_flush();
		}

		/* next position to fill */
		++state->progress_ptr;
		if (state->progress_ptr >= PROGRESS_MAX)
			state->progress_ptr -= PROGRESS_MAX;

		/* one more measure */
		++state->progress_tick;
	}

	/* stop if requested */
	if (global_interrupt) {
		/* LCOV_EXCL_START */
		if (!state->opt.gui) {
			log_fatal("\n");
			log_fatal("Stopping for interruption at block %u\n", blockpos);
		}
		log_tag("sigint:%u: SIGINT received\n", blockpos);
		log_flush();
		return 1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

void state_usage_waste(struct snapraid_state* state)
{
	uint64_t now = tick();

	state->tick_last = now;
}

void state_usage_misc(struct snapraid_state* state)
{
	uint64_t now = tick();
	uint64_t delta = now - state->tick_last;

	/* increment the time spent in computations */
	state->tick_misc += delta;

	state->tick_last = now;
}

void state_usage_sched(struct snapraid_state* state)
{
	uint64_t now = tick();
	uint64_t delta = now - state->tick_last;

	/* increment the time spent in computations */
	state->tick_sched += delta;

	state->tick_last = now;
}

void state_usage_raid(struct snapraid_state* state)
{
	uint64_t now = tick();
	uint64_t delta = now - state->tick_last;

	/* increment the time spent in computations */
	state->tick_raid += delta;

	state->tick_last = now;
}

void state_usage_hash(struct snapraid_state* state)
{
	uint64_t now = tick();
	uint64_t delta = now - state->tick_last;

	/* increment the time spent in computations */
	state->tick_hash += delta;

	state->tick_last = now;
}

void state_usage_file(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	(void)state;

	disk->progress_file = file;
}

void state_usage_disk(struct snapraid_state* state, struct snapraid_handle* handle_map, unsigned* waiting_map, unsigned waiting_mac)
{
	uint64_t now = tick();
	uint64_t delta = now - state->tick_last;
	unsigned i;

	/* increment the time spent in the data disks */
	for (i = 0; i < waiting_mac; ++i) {
		struct snapraid_disk* disk = handle_map[waiting_map[i]].disk;

		if (!disk)
			continue;

		disk->tick += delta;
	}
	state->tick_io += delta;

	state->tick_last = now;
}

void state_usage_parity(struct snapraid_state* state, unsigned* waiting_map, unsigned waiting_mac)
{
	uint64_t now = tick();
	uint64_t delta = now - state->tick_last;
	unsigned i;

	/* increment the time spent in the parity disk */
	for (i = 0; i < waiting_mac; ++i)
		state->parity[waiting_map[i]].tick += delta;
	state->tick_io += delta;

	state->tick_last = now;
}

void state_usage_print(struct snapraid_state* state)
{
	/* set the latest data */
	state_progress_latest(state);

	if (msg_level < MSG_PROGRESS)
		return;

	/* print a graph for it */
	state_progress_graph(state, 0, state->progress_ptr, PROGRESS_MAX);
}

void state_fscheck(struct snapraid_state* state, const char* ope)
{
	tommy_node* i;

	/* check the file-system on all disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		if (fs_check(disk) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Internal inconsistency in file-system for disk '%s' %s\n", disk->name, ope);
			os_abort();
			/* LCOV_EXCL_STOP */
		}
	}
}

void generate_configuration(const char* path)
{
	struct snapraid_state state;
	struct snapraid_content* content;
	char esc_buffer[ESC_MAX];
	unsigned l, s;
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
	printf("blocksize %u\n", state.block_size / KIBI);
	printf("\n");
	printf("# Use this hashsize\n");
	printf("hashsize %u\n", BLOCK_HASH_SIZE);
	printf("\n");
	for (l = 0; l < state.level; ++l) {
		printf("# Set the correct path for the %s files\n", lev_name(l));
		printf("# You had %u of them:\n", state.parity[l].split_mac);
		for (s = 0; s < state.parity[l].split_mac; ++s) {
			printf("# %u:\n", s);
			printf("# PATH:");
			if (state.parity[l].split_map[s].path[0])
				printf("%s", state.parity[l].split_map[s].path);
			else
				printf("?");
			printf("\n");
			printf("# SIZE:");
			if (state.parity[l].split_map[s].size != PARITY_SIZE_INVALID)
				printf("%" PRIu64, state.parity[l].split_map[s].size);
			else
				printf("?");
			printf("\n");
			printf("# UUID:");
			if (state.parity[l].split_map[s].uuid[0])
				printf("%s", state.parity[l].split_map[s].uuid);
			else
				printf("?");
			printf("\n");
			printf("#\n");
		}
		printf("%s ENTER_HERE_THE_PARITY_FILES_COMMA_SEPARATED\n", lev_config_name(l));
		printf("\n");
	}
	printf("# Add any other content file\n");
	printf("content %s\n", path);
	printf("\n");
	for (j = state.maplist; j; j = j->next) {
		struct snapraid_map* map = j->data;
		struct snapraid_disk* disk;
		printf("# Set the correct dir for disk '%s'\n", map->name);
		if (map->uuid[0])
			printf("# Disk '%s' is the one with id '%s'\n", map->name, map->uuid);
		disk = find_disk_by_name(&state, map->name);
		if (disk && disk->filelist) {
			struct snapraid_file* file = disk->filelist->data;
			if (file) {
				printf("# and containing: %s\n", fmt_poll(disk, file->sub, esc_buffer));
			}
		}
		printf("data %s ENTER_HERE_THE_DIR\n", map->name);
		printf("\n");
	}

	state_done(&state);
}

