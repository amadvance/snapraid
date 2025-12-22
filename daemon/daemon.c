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

#include "../civetweb/civetweb.h"

#include "state.h"
#include "support.h"

#define PID_FILE "/var/run/snapraidd.pid"

static volatile int running = 1;

struct snapraid_state STATE;

/* --- API Handlers --- */

static int send_json_success(struct mg_connection *conn, int status) 
{
	char body[256];
	
	int body_len = snprintf(body, sizeof(body), "{\n  \"success\": true\n}\n");

	mg_printf(conn, "HTTP/1.1 %d %s\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n\r\n", 
		status, mg_get_response_code_text(conn, status), body_len);

	mg_write(conn, body, body_len);

	return status;
}

static int send_json_error(struct mg_connection *conn, int status, const char* message) 
{
	char body[256];
	
	int body_len = snprintf(body, sizeof(body), 
		"{\n  \"success\": false,\n  \"message\": \"%s\"\n}\n", 
                message);

	mg_printf(conn, "HTTP/1.1 %d %s\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n\r\n", 
		status, mg_get_response_code_text(conn, status), body_len);

	mg_write(conn, body, body_len);

	return status;
}

static int handler_not_found(struct mg_connection* conn, void* cbdata)
{
	(void)cbdata;
	return send_json_error(conn, 404, "Resource not found");
}

/**
 * POST /api/v1/array/up, /down, /smart 
 * Triggers background tasks and returns 202 immediately
 */
static int handler_action(struct mg_connection* conn, void* cbdata) 
{
	const struct mg_request_info *ri = mg_get_request_info(conn);
	const char* path = ri->local_uri;

	(void)cbdata;

	if (strcmp(ri->request_method, "POST") != 0)
		return send_json_error(conn, 405, "Only POST is allowed for this endpoint");

	if (0) {
		return send_json_error(conn, 409, "A SnapRAID task is already running");
	}

	return send_json_success(conn, 200);
}

void json_device_list(ss_t* s, int tab, tommy_list* list)
{
	tommy_node* i;

	++tab;
	for (i = tommy_list_head(list); i; i = i->next) {
		struct snapraid_device* dev = i->data;
		ss_jsonf(s, tab, "{\n");
		++tab;
		if (*dev->family)
			ss_jsonf(s, tab, "\"family\": \"%s\",\n", dev->family);
		if (*dev->model)
			ss_jsonf(s, tab, "\"model\": \"%s\",\n", dev->model);
		if (*dev->serial)
			ss_jsonf(s, tab, "\"serial\": \"%s\",\n", dev->serial);
		if (dev->power != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"power\": \"%s\",\n", dev->power ? "active" : "standby");
		if (dev->health != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"health\": \"%s\",\n", dev->health ? "failing" : "ok");
		if (dev->size != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"size\": %" PRIu64 ",\n", dev->size);
		if (dev->rotational != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"rotational\": %" PRIu64 ",\n", dev->rotational);
		if (dev->error != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"error\": %" PRIu64 ",\n", dev->error);
		if (dev->smart[SMART_REALLOCATED_SECTOR_COUNT] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_reallocated_sector_count\": %" PRIu64 ",\n", dev->smart[SMART_REALLOCATED_SECTOR_COUNT] & 0xFFFFFFFF);
		if (dev->smart[SMART_UNCORRECTABLE_ERROR_CNT] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_uncorrectable_error_cnt\": %" PRIu64 ",\n", dev->smart[SMART_UNCORRECTABLE_ERROR_CNT] & 0xFFFF);
		if (dev->smart[SMART_COMMAND_TIMEOUT] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_command_timeout\": %" PRIu64 ",\n", dev->smart[SMART_COMMAND_TIMEOUT] & 0xFFFF);
		if (dev->smart[SMART_CURRENT_PENDING_SECTOR] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_current_pending_sector\": %" PRIu64 ",\n", dev->smart[SMART_CURRENT_PENDING_SECTOR] & 0xFFFFFFFF);
		if (dev->smart[SMART_OFFLINE_UNCORRECTABLE] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_offline_uncorrectable\": %" PRIu64 ",\n", dev->smart[SMART_OFFLINE_UNCORRECTABLE] & 0xFFFFFFFF);
		if (dev->smart[SMART_START_STOP_COUNT] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_start_stop_count\": %" PRIu64 ",\n", dev->smart[SMART_START_STOP_COUNT] & 0xFFFFFFFF);
		if (dev->smart[SMART_LOAD_CYCLE_COUNT] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_power_on_hours\": %" PRIu64 ",\n", dev->smart[SMART_LOAD_CYCLE_COUNT] & 0xFFFFFFFF);
		if (dev->smart[SMART_POWER_ON_HOURS] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_load_cycle_count\": %" PRIu64 ",\n", dev->smart[SMART_POWER_ON_HOURS] & 0xFFFFFFFF);
		if (dev->smart[SMART_TEMPERATURE_CELSIUS] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_temperature_celsius\": %" PRIu64 ",\n", dev->smart[SMART_TEMPERATURE_CELSIUS] & 0xFFFFFFFF);
		else if (dev->smart[SMART_AIRFLOW_TEMPERATURE_CELSIUS] != SMART_UNASSIGNED)
			ss_jsonf(s, tab, "\"smart_temperature_celsius\": %" PRIu64 ",\n", dev->smart[SMART_AIRFLOW_TEMPERATURE_CELSIUS] & 0xFFFFFFFF);
		ss_jsonf(s, tab, "\"device_node\": \"%s\"\n", dev->file);
		--tab;
		ss_jsonf(s, tab, "}%s\n", i->next ? "," : "");
		--tab;
	}
	--tab;
}

/**
 * GET /api/v1/array/disks
 * Returns detailed disk status lists
 */
static int handler_disks(struct mg_connection* conn, void* cbdata) 
{
	tommy_node* i;
	tommy_node* j;
	int tab = 0;
	ss_t s;
	ss_init(&s);
	
	(void)cbdata;

	ss_jsonf(&s, 0, "{\n");
	++tab;
	ss_jsonf(&s, 1, "\"data_disks\": [\n");
	for (i = tommy_list_head(&STATE.data_list); i; i = i->next) {
		struct snapraid_data* data = i->data;

		++tab;
		ss_jsonf(&s, tab, "{\n");
		++tab;
		ss_jsonf(&s, tab, "\"name\": \"%s\",\n", data->name);
		ss_jsonf(&s, tab, "\"mount_dir\": \"%s\",\n", data->dir);
		if (*data->uuid)
			ss_jsonf(&s, tab, "\"uuid\": \"%s\",\n", data->uuid);
		if (*data->content_uuid)
			ss_jsonf(&s, tab, "\"stored_uuid\": \"%s\",\n", data->content_uuid);
		if (data->content_size != SMART_UNASSIGNED)
			ss_jsonf(&s, tab, "\"allocated_space_bytes\": %" PRIu64 ",\n", data->content_size);
		if (data->content_free != SMART_UNASSIGNED)
			ss_jsonf(&s, tab, "\"free_space_bytes\": %" PRIu64 ",\n", data->content_free);
		ss_jsonf(&s, tab, "\"devices\": [\n");
		json_device_list(&s, tab, &data->device_list); 
		ss_jsonf(&s, tab, "]\n");
		--tab;
		ss_jsonf(&s, tab, "}%s\n", i->next ? "," : "");
		--tab;
	}
	ss_jsonf(&s, tab, "],\n");
	ss_jsonf(&s, tab, "\"parity_disks\": [\n");
	for (i = tommy_list_head(&STATE.parity_list); i; i = i->next) {
		struct snapraid_parity* parity = i->data;

		++tab;
		ss_jsonf(&s, tab, "{\n");
		++tab;
		ss_jsonf(&s, tab, "\"name\": \"%s\",\n", parity->name);
		if (parity->content_size != SMART_UNASSIGNED)
			ss_jsonf(&s, tab, "\"allocated_space_bytes\": %" PRIu64 ",\n", parity->content_size);
		if (parity->content_free != SMART_UNASSIGNED)
			ss_jsonf(&s, tab, "\"free_space_bytes\": %" PRIu64 ",\n", parity->content_free);
		ss_jsonf(&s, tab, "\"splits\": [\n");

		for (j = tommy_list_head(&parity->split_list); j; j = j->next) {
			struct snapraid_split* split = j->data;

			++tab;
			ss_jsonf(&s, tab, "{\n");
			++tab;
			ss_jsonf(&s, tab, "\"parity_path\": \"%s\",\n", split->path);
			if (*split->uuid)
				ss_jsonf(&s, tab, "\"uuid\": \"%s\",\n", split->uuid);
			if (*split->content_uuid)
				ss_jsonf(&s, tab, "\"stored_uuid\": \"%s\",\n", split->content_uuid);
			ss_jsonf(&s, tab, "\"devices\": [\n");
			json_device_list(&s, tab, &split->device_list); 
			ss_jsonf(&s, tab, "]\n");
			--tab;
			ss_jsonf(&s, tab, "}%s\n", j->next ? "," : "");
			--tab;
		}

		ss_jsonf(&s, tab, "]\n");
		--tab;
		ss_jsonf(&s, tab, "}%s\n", i->next ? "," : "");
		--tab;
	}
	ss_jsonf(&s, tab, "]\n");
	--tab;
	ss_jsonf(&s, tab, "}\n");

	mg_printf(conn, "HTTP/1.1 200 OK\r\n");
	mg_printf(conn, "Content-Type: application/json\r\n");
	mg_printf(conn, "Content-Length: %lu\r\n", ss_len(&s));
	mg_printf(conn, "Connection: close\r\n");
	mg_printf(conn, "\r\n");

	mg_write(conn, ss_ptr(&s), ss_len(&s)); 

	ss_done(&s);

	return 200;
}

/**
 * GET /api/v1/task/progress
 * Poll current task progress for front-end updates
 */
static int handler_progress(struct mg_connection* conn, void* cbdata)
{
	int tab = 0;
	ss_t s;

	(void)cbdata;

	ss_init(&s);

	ss_jsons(&s, tab, "{\n");
	++tab;
	ss_jsonf(&s, tab, "\"command\": \"sync\",\n");
	ss_jsonf(&s, tab, "\"status\": \"running\",\n");
	ss_jsonf(&s, tab, "\"percent\": 42,\n");
	ss_jsonf(&s, tab, "\"speed_bs\": 150000000,\n");
	ss_jsonf(&s, tab, "\"eta_seconds\": 3600,\n"); 
	ss_jsonf(&s, tab, "\"errors\": [\n");
	for (int i = 0; i < 1; i++) { // Dummy loop for TaskError items
		++tab;
		ss_jsonf(&s, tab, "{\n");
		++tab;
		ss_jsonf(&s, tab, "\"reference_type\": \"file\",\n");
		ss_jsonf(&s, tab, "\"message\": \"checksum error\",\n");
		ss_jsonf(&s, tab, "\"disk_name\": \"d1\",\n");
		ss_jsonf(&s, tab, "\"path\": \"/mnt/d1/data/file.txt\",\n");
		ss_jsonf(&s, tab, "\"block_number\": 123456\n");
		--tab;
		ss_jsonf(&s, tab, "}\n");
		--tab;
	}
	ss_jsonf(&s, tab, "]\n");
	--tab;
	ss_jsonf(&s, tab, "}\n");

	mg_printf(conn, "HTTP/1.1 200 OK\r\n");
	mg_printf(conn, "Content-Type: application/json\r\n");
	mg_printf(conn, "Content-Length: %lu\r\n", ss_len(&s));
	mg_printf(conn, "Connection: close\r\n");
	mg_printf(conn, "\r\n");

	mg_write(conn, ss_ptr(&s), ss_len(&s)); 

	ss_done(&s);

	return 200;
}

static void handle_signal(int sig)
{
	(void)sig;
	running = 0;
}

static int daemonize(void)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid > 0)
		exit(EXIT_SUCCESS);

	if (setsid() < 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid > 0)
		exit(EXIT_SUCCESS);

	umask(0);
	chdir("/");

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	int fd = open("/dev/null", O_RDWR);
	if (fd >= 0) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
	}

	return 0;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"Options:\n"
		"  -f, --foreground   Run in foreground (do not daemonize)\n",
		prog);
}

int main(int argc, char *argv[])
{
	int foreground = 1; // TODO

	lock_init();

	static const struct option long_opts[] = {
		{ "foreground", no_argument, 0, 'f' },
		{ 0, 0, 0, 0 }
	};
	
	static const char* options[] = {
		"listening_ports", "8080",
		"num_threads", "50",
		NULL
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "f", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'f':
			foreground = 1;
			break;
		default:
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (!foreground) {
		if (daemonize() < 0)
			exit(EXIT_FAILURE);
	}

	signal(SIGTERM, handle_signal);
	signal(SIGINT, handle_signal);

	struct mg_callbacks callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	struct mg_context* ctx = mg_start(&callbacks, 0, options);
	if (!ctx) {
		exit(EXIT_FAILURE);
	}

	mg_set_request_handler(ctx, "/api/v1/probe$", handler_action, NULL);
	mg_set_request_handler(ctx, "/api/v1/up$", handler_action, NULL);
	mg_set_request_handler(ctx, "/api/v1/down$", handler_action, NULL);
	mg_set_request_handler(ctx, "/api/v1/smart$", handler_action, NULL);
	mg_set_request_handler(ctx, "/api/v1/disks$", handler_disks, NULL);
	mg_set_request_handler(ctx, "/api/v1/progress$", handler_progress, NULL);
	mg_set_request_handler(ctx, "/api", handler_not_found, NULL);

	/* 
	 * Load basing info into the state.
	 * 
	 * This is an immediate command that doesn't wake up disks and neither load the content file
	 */
	runner("probe", &STATE);

	printf("Running...\n");

	while (running)
		sleep(1);

	mg_stop(ctx);

	if (!foreground)
		unlink(PID_FILE);

	lock_done();

	return 0;
}


/*
curl -X POST http://localhost:8080/api/v1/array/up
curl -X POST http://localhost:8080/api/v1/array/down
curl -X POST http://localhost:8080/api/v1/array/smart

curl -X GET http://localhost:8080/api/v1/array/disks
curl -X GET http://localhost:8080/api/v1/task/progress
*/
