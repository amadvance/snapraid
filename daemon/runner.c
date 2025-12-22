/*
 * Copyright (C) 2025 Andrea Mazzoleni
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

#include "state.h"
#include "support.h"

pid_t runner_spawn(char *const argv[], int* stderr_fd) 
{
	int err_pipe[2];
	int devnull;
	pid_t pid;

	if (pipe(err_pipe) < 0)
		return -1;

	devnull = open("/dev/null", O_WRONLY);
	if (devnull < 0) {
		close(err_pipe[0]);
		close(err_pipe[1]);
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		close(err_pipe[0]);
		close(err_pipe[1]);
		close(devnull);
		return -1;
	}

	if (pid == 0) {
		/* child */

		/* stdout -> /dev/null */
		dup2(devnull, STDOUT_FILENO);

		/* stderr -> pipe */
		dup2(err_pipe[1], STDERR_FILENO);

		close(err_pipe[0]);
		close(err_pipe[1]);
		close(devnull);

		execvp(argv[0], argv);
		_exit(127);
	}

	/* parent */
	close(err_pipe[1]);
	close(devnull);

	*stderr_fd = err_pipe[0];
	return pid;
}

int is_parity(const char* s)
{
	if (isdigit(s[0]) && s[1] == '-')
		s += 2;

	return strcmp(s, "parity") == 0;
}

int split_parity(char* s, int* index)
{
	if (isdigit(s[0]) && s[1] == '-')
		s += 2;

	if (strncmp(s, "parity", 6) != 0)
		return 0;

	s += 6;

	if (s[0] == 0) {
		*index = 0;
		return 1;
	} 

	if (s[0] == '/') {
		*s = 0;
		if (si(index, s + 1) == 0)
			return 1;
	}

	return 0;
}

struct snapraid_data* find_data(tommy_list* list, const char* name)
{
	struct snapraid_data* data;
	tommy_node* i;

	i = tommy_list_head(list);
	while (i) {
		data = i->data;
		if (strcmp(name, data->name) == 0) 
			return data;
		i = i->next;
	}

	data = calloc_nofail(1, sizeof(struct snapraid_data));
	data->content_size = SMART_UNASSIGNED;
	data->content_free = SMART_UNASSIGNED;
	scpy(data->name, sizeof(data->name), name);
	tommy_list_insert_tail(list, &data->node, data);

	return data;
}

struct snapraid_parity* find_parity(tommy_list* list, const char* name)
{
	struct snapraid_parity* parity;
	tommy_node* i;

	i = tommy_list_head(list);
	while (i) {
		parity = i->data;
		if (strcmp(name, parity->name) == 0) 
			return parity;
		i = i->next;
	}

	parity = calloc_nofail(1, sizeof(struct snapraid_parity));
	parity->content_size = SMART_UNASSIGNED;
	parity->content_free = SMART_UNASSIGNED;
	scpy(parity->name, sizeof(parity->name), name);
	tommy_list_insert_tail(list, &parity->node, parity);

	return parity;
}

struct snapraid_split* find_split(tommy_list* list, int index)
{
	struct snapraid_split* split;
	tommy_node* i;

	i = tommy_list_head(list);
	while (i) {
		split = i->data;
		if (index == split->index)
			return split;
		i = i->next;
	}

	split = calloc_nofail(1, sizeof(struct snapraid_split));
	split->index = index;
	tommy_list_insert_tail(list, &split->node, split);

	return split;
}

struct snapraid_device* find_device_from_file(tommy_list* list, const char* file)
{
	struct snapraid_device* device;
	tommy_node* i;
	int j;

	i = tommy_list_head(list);
	while (i) {
		device = i->data;
		if (strcmp(file, device->file) == 0)
			return device;
		i = i->next;
	}

	device = calloc_nofail(1, sizeof(struct snapraid_device));
	for (j = 0;j < SMART_COUNT;++ j)
		device->smart[j] = SMART_UNASSIGNED;
	device->error = SMART_UNASSIGNED;
	device->size = SMART_UNASSIGNED;
	device->rotational = SMART_UNASSIGNED;
	device->error = SMART_UNASSIGNED;
	device->flags = SMART_UNASSIGNED;
	device->power = SMART_UNASSIGNED;
	device->health = SMART_UNASSIGNED;
	scpy(device->file, sizeof(device->file), file);
	tommy_list_insert_tail(list, &device->node, device);

	return device;
}

struct snapraid_device* find_device(struct snapraid_state* state, char* name, const char* file)
{
	int index;

	if (split_parity(name, &index)) {
		struct snapraid_parity* parity = find_parity(&state->parity_list, name);
		struct snapraid_split* split = find_split(&parity->split_list, index);
		return find_device_from_file(&split->device_list, file);
	} else {
		struct snapraid_data* data = find_data(&state->data_list, name);
		return find_device_from_file(&data->device_list, file);
	}
}

void process_data(struct snapraid_state* state, char** map, size_t mac)
{
	struct snapraid_data* data;

	if (mac < 4)
		return;

	data = find_data(&state->data_list, map[1]);

	scpy(data->dir, sizeof(data->dir), map[2]);
	scpy(data->uuid, sizeof(data->uuid), map[3]);
}

void process_content_data(struct snapraid_state* state, char** map, size_t mac)
{
	struct snapraid_data* data;

	if (mac < 3)
		return;

	data = find_data(&state->data_list, map[1]);

	scpy(data->content_uuid, sizeof(data->content_uuid), map[2]);
}

void process_parity(struct snapraid_state* state, char** map, size_t mac)
{
	struct snapraid_parity* parity;
	struct snapraid_split* split;
	int index;

	if (mac < 3)
		return;
	if (!split_parity(map[0], &index)) 
		return;

	parity = find_parity(&state->parity_list, map[0]);
	split = find_split(&parity->split_list, index);

	scpy(split->path, sizeof(split->path), map[1]);
	scpy(split->uuid, sizeof(split->uuid), map[2]);
}

void process_content_parity(struct snapraid_state* state, char** map, size_t mac)
{
	struct snapraid_parity* parity;
	struct snapraid_split* split;
	int index;

	if (mac < 4)
		return;
	if (!split_parity(map[0], &index)) 
		return;

	parity = find_parity(&state->parity_list, map[0]);
	split = find_split(&parity->split_list, index);

	scpy(split->content_uuid, sizeof(split->content_uuid), map[1]);
	scpy(split->content_path, sizeof(split->content_path), map[2]);
	su64(&split->content_size, map[3]);
}

void process_content_allocation(struct snapraid_state* state, char** map, size_t mac)
{
	if (mac < 3)
		return;

	if (is_parity(map[0])) {
		struct snapraid_parity* parity = find_parity(&state->parity_list, map[0]);
		su64(&parity->content_size, map[1]);
		su64(&parity->content_free, map[2]);
	} else {
		struct snapraid_data* data = find_data(&state->data_list, map[0]);
		su64(&data->content_size, map[1]);
		su64(&data->content_free, map[2]);
	}
}

void process_attr(struct snapraid_state* state, char** map, size_t mac)
{
	const char* tag;
	const char* val;
	struct snapraid_device* device;

	if (mac < 5)
		return;
	if (map[2][0] == 0) /* ignore if no disk name is provided */
		return;

	device = find_device(state, map[2], map[1]);

	tag = map[3];
	val = map[4];
	
	if (strcmp(tag, "serial") == 0)
		scpy(device->serial, sizeof(device->serial), val);
	else if (strcmp(tag, "model") == 0)
		scpy(device->model, sizeof(device->model), val);
	else if (strcmp(tag, "family") == 0)
		scpy(device->family, sizeof(device->family), val);
	else if (strcmp(tag, "size") == 0)
		su64(&device->size, val);
	else if (strcmp(tag, "rotationrate") == 0)
		su64(&device->rotational, val);
//	else if (strcmp(tag, "afr") == 0)
		//device->info[ROTATION_RATE] = si64(val);
	else if (strcmp(tag, "error") == 0)
		su64(&device->error, val);
	else if (strcmp(tag, "power") == 0) {
		device->power = SMART_UNASSIGNED;
		if (strcmp(tag, "standby") == 0 || strcmp(tag, "down") == 0)
			device->power = POWER_STANDBY;
		else if (strcmp(tag, "active") == 0  || strcmp(tag, "up") == 0)
			device->power = POWER_ACTIVE;
	} else if (strcmp(tag, "flags") == 0) {
		device->health = SMART_UNASSIGNED;
		if (su64(&device->flags, val) == 0) {
			if (device->flags & (SMARTCTL_FLAG_FAIL | SMARTCTL_FLAG_PREFAIL))
				device->health = HEALTH_FAILING;
			else
				device->health = HEALTH_PASSED;
		}
	} else {
		int index;
		if (si(&index, tag) == 0) {
			if (index >= 0 && index < 256)
				su64(&device->smart[index], val);
		}
	}
}

void process_conf(struct snapraid_state* state, char** map, size_t mac)
{
	if (mac >= 2 && strcmp(map[1], "file") == 0) {
		scpy(state->conf, sizeof(state->conf), map[2]);
	}
}

void process_line(struct snapraid_state* state, char** map, size_t mac)
{
	const char* cmd;

	if (mac == 0)
		return;

	cmd = map[0];

	if (strcmp(cmd, "data") == 0) {
		process_data(state, map, mac);
	} else if (is_parity(cmd)) {
		process_parity(state, map, mac);
//	} else if (strcmp(cmd, "device") == 0) {
		//process_device(state, map, mac);
	} else if (strcmp(cmd, "attr") == 0) {
		process_attr(state, map, mac);
	} else if (strcmp(cmd, "conf") == 0) {
		process_conf(state, map, mac);
	} else if (strcmp(cmd, "content_disk") == 0) {
		process_content_data(state, map, mac);
	} else if (strcmp(cmd, "content_parity") == 0) {
		process_content_parity(state, map, mac);
	} else if (strcmp(cmd, "content_allocation") == 0) {
		process_content_allocation(state, map, mac);
	}
}

#define RUN_INPUT_MAX 4096
#define RUN_FIELD_MAX 64

void process_stderr(struct snapraid_state* state, int f)
{
	char buf[RUN_INPUT_MAX];
	char line[RUN_INPUT_MAX];
	char* map[RUN_FIELD_MAX];
	size_t len = 0;
	size_t mac = 0;
	int escape = 0;

	map[mac++] = line;

	while (1) {
		ssize_t n = read(f, buf, sizeof(buf));
		if (n > 0) {
			ssize_t i;
			for (i = 0; i < n; i++) {
				char c = buf[i];

				if (escape) {
					if (len + 1 < RUN_INPUT_MAX) { /* ignore if too long */
						switch (c) {
						case '\\' : line[len++] = '\\'; break;
						case 'n' :  line[len++] = '\n'; break;
						case 'd' : line[len++] = ':'; break;
						default: /* ignore if unknown */
						}
					}
					escape = 0;
					continue;
				}

				if (c == '\\') {
					escape = 1;
					continue;
				}

				if (c == ':') {
					if (mac + 1 < RUN_FIELD_MAX) {
						line[len++] = '\0';
						map[mac++] = &line[len];
						continue;
					}
					/* do not split if too many fields */
				}

				if (c == '\n') {
					line[len] = '\0';
					map[mac] = 0;

					process_line(state, map, mac);

					len = 0;
					mac = 0;
					escape = 0;
					map[mac++] = line;
					continue;
				}

				if (len + 1 < RUN_INPUT_MAX) /* ignore if too long */
					line[len++] = c;
			}
		} else if (n == 0) {
			/* EOF, discard partial read not ending with \n */
			break;
		} else { /* n < 0 */
			if (errno == EINTR) {
				continue;
			} else {
				break;
			}
		}
	}
}

int runner(const char* cmd, struct snapraid_state* state)
{
	pid_t pid;
	int stderr_f;
	char* argv[5];

	argv[0] = "snapraid";
	argv[1] = (char*)cmd;
	argv[2] = "--log";
	argv[3] = ">&2";
	argv[4] = 0;

	pid = runner_spawn(argv, &stderr_f);
	if (pid < 0)
		return -1;

	process_stderr(state, stderr_f);

	close(stderr_f);
	return 0;
}
