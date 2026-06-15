// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "portable.h"

#ifdef __MINGW32__ /* Only for MingW */

#include "support.h"

/****************************************************************************/
/* global */

/**
 * Exit codes.
 */
int exit_success = 0;
int exit_failure = 1;
int exit_sync_needed = 2;

/****************************************************************************/
/* signal */

volatile int global_interrupt = 0;

int app_global_interrupt(void)
{
	return global_interrupt;
}

void app_signal_handler(int signum)
{
	/* report the request of interruption with the signal received */
	global_interrupt = signum;
}

/****************************************************************************/
/* resolve */

/**
 * Get the device file from a path inside the device.
 */
static int devresolve(const char* mount, char* file, size_t file_size, char* wfile, size_t wfile_size)
{
	wchar_t conv_buf_mount[CONV_MAX];
	char conv_buf_volume_guid[CONV_MAX];
	WCHAR volume_mount[PATH_MAX];
	WCHAR volume_guid[PATH_MAX];
	DWORD i;
	char* p;

	/* get the volume mount point from the disk path */
	if (!GetVolumePathNameW(convert(conv_buf_mount, mount), volume_mount, sizeof(volume_mount) / sizeof(WCHAR))) {
		windows_errno(GetLastError());
		return -1;
	}

	/* get the volume GUID path from the mount point */
	if (!GetVolumeNameForVolumeMountPointW(volume_mount, volume_guid, sizeof(volume_guid) / sizeof(WCHAR))) {
		windows_errno(GetLastError());
		return -1;
	}

	/*
	 * Remove the final slash, otherwise CreateFile() opens the file-system
	 * and not the volume
	 */
	i = 0;
	while (volume_guid[i] != 0)
		++i;
	if (i != 0 && volume_guid[i - 1] == '\\')
		volume_guid[i - 1] = 0;

	pathcpy(wfile, wfile_size, u16tou8(conv_buf_volume_guid, volume_guid));

	/* get the GUID start { */
	p = strchr(wfile, '{');
	if (!p)
		p = wfile;
	else
		++p;

	pathprint(file, file_size, "/dev/vol%s", p);

	/* cut GUID end } */
	p = strrchr(file, '}');
	if (p)
		*p = 0;

	return 0;
}

/****************************************************************************/
/* uuid */

int devuuid(uint64_t device_id, const char* device_path, char* uuid, size_t uuid_size)
{
	(void)device_path;

	/* just use the volume serial number returned in the device parameter */
	snprintf(uuid, uuid_size, "%08x", (unsigned)device_id);

	log_tag("uuid:windows:%u:%s:\n", (unsigned)device_id, uuid);

	return 0;
}

/****************************************************************************/
/* dev */

/**
 * Read a device tree filling the specified list of disk_t entries.
 */
static int devtree(devinfo_t* parent, tommy_list* list)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	unsigned char vde_buffer[sizeof(VOLUME_DISK_EXTENTS)];
	VOLUME_DISK_EXTENTS* vde = (VOLUME_DISK_EXTENTS*)&vde_buffer;
	unsigned vde_size = sizeof(vde_buffer);
	void* vde_alloc = 0;
	BOOL ret;
	DWORD n;
	DWORD i;

	/* open the volume */
	h = CreateFileW(convert(conv_buf, parent->wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	/* get the physical extents of the volume */
	ret = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, 0, 0, vde, vde_size, &n, 0);
	if (!ret) {
		DWORD error = GetLastError();
		if (error != ERROR_MORE_DATA) {
			CloseHandle(h);
			windows_errno(error);
			return -1;
		}

		/* more than one extends, allocate more space */
		vde_size = sizeof(VOLUME_DISK_EXTENTS) + vde->NumberOfDiskExtents * sizeof(DISK_EXTENT);
		vde_alloc = malloc_nofail(vde_size);
		vde = vde_alloc;

		/* retry with more space */
		ret = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, 0, 0, vde, vde_size, &n, 0);
	}
	if (!ret) {
		DWORD error = GetLastError();
		CloseHandle(h);
		free(vde_alloc);
		windows_errno(error);
		return -1;
	}

	for (i = 0; i < vde->NumberOfDiskExtents; ++i) {
		devinfo_t* devinfo;

		devinfo = calloc_nofail(1, sizeof(devinfo_t));

		pathcpy(devinfo->name, sizeof(devinfo->name), parent->name);
		pathcpy(devinfo->smartctl, sizeof(devinfo->smartctl), parent->smartctl);
		memcpy(devinfo->smartignore, parent->smartignore, sizeof(devinfo->smartignore));
		devinfo->device = vde->Extents[i].DiskNumber;
		pathprint(devinfo->file, sizeof(devinfo->file), "/dev/pd%" PRIu64, devinfo->device);
		pathprint(devinfo->wfile, sizeof(devinfo->wfile), "\\\\.\\PhysicalDrive%" PRIu64, devinfo->device);
		devinfo->parent = parent;

		/* insert in the list */
		tommy_list_insert_tail(list, &devinfo->node, devinfo);
	}

	if (!CloseHandle(h)) {
		windows_errno(GetLastError());
		free(vde_alloc);
		return -1;
	}

	free(vde_alloc);

	return 0;
}

/**
 * Compute disk usage by aggregating access statistics.
 *
 * On many Windows versions, these raw disk performance counters are disabled by default to save overhead.
 *
 * If DeviceIoControl call with IOCTL_DISK_PERFORMANCE returns zeros or fails, the counters are likely disabled.
 *
 * Run the following command from an elevated command prompt:
 *
 *   diskperf -y
 *
 * This change usually requires a reboot to take effect at the driver level.
 */
static int devstat(uint64_t device, const char* name, const char* wfile, uint64_t* count)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	BOOL ret;
	DISK_PERFORMANCE ds;
	DWORD bytes;
	char file[128];

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* open the volume */
	h = CreateFileW(convert(conv_buf, wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	bytes = 0;
	ret = DeviceIoControl(h, IOCTL_DISK_PERFORMANCE, NULL, 0, &ds, sizeof(ds), &bytes, NULL);
	if (!ret) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	if (!CloseHandle(h)) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	*count = ds.ReadCount + ds.WriteCount;

	return 0;
}

/**
 * Get SMART attributes.
 */
static int devsmart(uint64_t device, const char* name, const char* smartctl, const char* smartctl_info, struct smart_attr* smart, uint64_t* info, char* serial, char* family, char* model, char* inter)
{
	char conv_buf[CONV_MAX];
	WCHAR cmd[PATH_MAX + SMART_MAX];
	char file[128];
	FILE* f;
	int ret;
	int count;

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	const char* info_opts = smartctl_info[0] ? smartctl_info : "-a";

	/* if there is a custom smartctl command */
	if (smartctl[0]) {
		char option[SMART_MAX];
		snprintf(option, sizeof(option), smartctl, file);
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" %s %s", windows_exedir(), info_opts, option);
	} else {
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" %s %s", windows_exedir(), info_opts, file);
	}

	count = 0;

retry:
	log_tag("smartctl:%s:%s:run: %s\n", file, name, u16tou8(conv_buf, cmd));

	f = _wpopen(cmd, L"rt");
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from popen).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_attribute(f, file, name, smart, info, serial, family, model, inter) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		log_tag("device:%s:%s:shell\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from pclose).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if first try without custom smartctl command */
	if (count == 0 && smartctl[0] == 0) {
		/*
		 * Handle some common cases in Windows.
		 *
		 * Sometimes the "type" autodetection is wrong, and the command fails at identification
		 * stage, returning with error 2, or even with error 0, and with no info at all.
		 * We detect this condition checking the PowerOnHours, Size and RotationRate attributes.
		 *
		 * In such conditions we retry using the "sat" type, that often allows to proceed.
		 *
		 * Note that getting error 4 is instead very common, even with full info gathering.
		 */
		if ((ret == 0 || ret == 2)
			&& smart[SMART_POWER_ON_HOURS].raw == SMART_UNASSIGNED
			&& info[INFO_SIZE] == SMART_UNASSIGNED
			&& info[INFO_ROTATION_RATE] == SMART_UNASSIGNED
		) {
			/* retry using the "sat" type */
			snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" %s -d sat %s", windows_exedir(), info_opts, file);

			++count;
			goto retry;
		}
	}

	/* store the smartctl return value */
	smart[SMART_FLAGS].raw = ret;

	return 0;
}

static int serial_descriptor(PSTORAGE_DEVICE_DESCRIPTOR descriptor, size_t size, char* output, size_t output_size)
{
	if (descriptor->SerialNumberOffset == 0
		|| descriptor->SerialNumberOffset == (DWORD)-1
		|| descriptor->SerialNumberOffset >= size)
		return -1;

	const char* raw = (const char*)descriptor + descriptor->SerialNumberOffset;
	size_t len = 0;

	/* find the length, not assuming a 0 ending */
	while (descriptor->SerialNumberOffset + len < size && raw[len] != 0)
		++len;

	if (len < 6) /* a serial cannot be so short */
		return -1;

	if (len + 1 > output_size)
		return -1;

	/* all must be printable */
	for (size_t i = 0; i < len; ++i) {
		if (!isprint((unsigned char)raw[i]))
			return -1;
	}

	memcpy(output, raw, len);
	output[len] = 0;

	strtrim(output);

	return 0;
}

static void devattr_property(HANDLE h, char* serial, char* model, char* inter)
{
	STORAGE_PROPERTY_QUERY query = { 0 };
	STORAGE_DESCRIPTOR_HEADER header = { 0 };
	DWORD bytes;

	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;
	if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &header, sizeof(header), &bytes, 0))
		return;
	if (header.Size == 0)
		return;

	/* allocate buffer and query full descriptor */
	BYTE* buffer = malloc(header.Size);
	if (!buffer)
		return;

	if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, header.Size, &bytes, 0)) {
		free(buffer);
		return;
	}

	STORAGE_DEVICE_DESCRIPTOR* desc = (STORAGE_DEVICE_DESCRIPTOR*)buffer;

	/* extract strings (offsets are from start of descriptor) */
	if (model && *model == 0 && desc->ProductIdOffset) {
		snprintf(model, SMART_MAX, "%s", (char*)(buffer + desc->ProductIdOffset));
		strtrim(model);
	}

	if (serial && *serial == 0) {
		/*
		 * This is the RAW serial, maybe HEX encoded, maybe BYTES SWAPPED,
		 * but we don't care because we just need any identifier.
		 *
		 * If smartctl was not able to read it, we now accept anything.
		 */
		serial_descriptor(desc, bytes, serial, SMART_MAX);
	}

#define BusTypeVirtual            0x0E
#define BusTypeFileBackedVirtual  0x0F
#define BusTypeSpaces             0x10
#define BusTypeNvme               0x11
#define BusTypeSCM                0x12
#define BusTypeUfs                0x13
#define BusTypeNvmeof             0x14

	const char* type = 0;
	switch ((int)desc->BusType) { /* cast to int to avoid warnings about enum unlisted */
	case BusTypeScsi : type = "SCSI"; break;
	case BusTypeAtapi : type = "ATAPI"; break;
	case BusTypeAta : type = "ATA"; break;
	case BusType1394 : type = "FireWire"; break;
	case BusTypeSsa : type = "SSA"; break;
	case BusTypeFibre : type = "Fibre"; break;
	case BusTypeUsb : type = "USB"; break;
	case BusTypeRAID : type = "RAID"; break;
	case BusTypeiScsi : type = "iSCSI"; break;
	case BusTypeSas : type = "SAS"; break;
	case BusTypeSata : type = "SATA"; break;
	case BusTypeSd : type = "SD"; break;
	case BusTypeMmc : type = "MMC"; break;
	case BusTypeVirtual : type = "Virtual"; break;
	case BusTypeFileBackedVirtual : type = "Virtual"; break;
	case BusTypeSpaces : type = "Storage Spaces"; break;
	case BusTypeNvme : type = "NVMe"; break;
	case BusTypeSCM : type = "SCM"; break;
	case BusTypeUfs : type = "UFS"; break;
	case BusTypeNvmeof : type = "NVMe"; break;
	default :
	}
	if (inter && type) /* always override interface as more detailed than smartctl */
		snprintf(inter, SMART_MAX, "%s", type);

	free(buffer);
}

static void devattr_size(HANDLE h, uint64_t* size)
{
	DISK_GEOMETRY_EX geom = { 0 };
	DWORD bytes;
	if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 0, 0, &geom, sizeof(geom), &bytes, 0))
		return;

	*size = geom.DiskSize.QuadPart;
}

static void devattr_rotational(HANDLE h, uint64_t* rotational)
{
	STORAGE_PROPERTY_QUERY query = { 0 };
	DEVICE_SEEK_PENALTY_DESCRIPTOR desc = { 0 };
	DWORD bytes;

	query.PropertyId = StorageDeviceSeekPenaltyProperty;
	query.QueryType = PropertyStandardQuery;
	if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &desc, sizeof(desc), &bytes, 0))
		return;
	if (bytes < sizeof(desc))
		return;
	if (desc.Version != sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR) || desc.Size != sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR))
		return;

	*rotational = desc.IncursSeekPenalty ? 1 : 0;
}

/**
 * Get device attributes.
 */
static void devattr(uint64_t device, const char* name, const char* wfile, uint64_t* info, char* serial, char* family, char* model, char* interf)
{
	HANDLE h;
	wchar_t conv_buf[CONV_MAX];
	char file[128];

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	(void)family; /* not available, smartctl uses an internal database to get it */

	/* open the volume */
	h = CreateFileW(convert(conv_buf, wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return;
	}

	if (info[INFO_SIZE] == SMART_UNASSIGNED)
		devattr_size(h, &info[INFO_SIZE]);

	if (*model == 0 || *serial == 0 || *interf == 0)
		devattr_property(h, serial, model, interf);

	/* set NVMe as not rotational */
	if (info[INFO_ROTATION_RATE] == SMART_UNASSIGNED && strcasecmp(interf, "nvme") == 0)
		info[INFO_ROTATION_RATE] = 0;

	/* set SSD as not rotational */
	if (info[INFO_ROTATION_RATE] == SMART_UNASSIGNED)
		devattr_rotational(h, &info[INFO_ROTATION_RATE]);

	if (!CloseHandle(h)) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return;
	}
}

/**
 * Check if the device needs power management (it's a rotational one)
 */
static int devpower(uint64_t device, const char* name, const char* wfile)
{
	HANDLE h;
	wchar_t conv_buf[CONV_MAX];
	char file[128];
	uint64_t rotational;
	char interf[SMART_MAX];

	rotational = SMART_UNASSIGNED;
	interf[0] = 0;
	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* open the volume */
	h = CreateFileW(convert(conv_buf, wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return 0; /* assume not rotational */
	}

	/* get the interface */
	devattr_property(h, 0, 0, interf);

	/* set NVMe as not rotational */
	if (strcasecmp(interf, "nvme") == 0)
		rotational = 0;

	/* set SSD as not rotational */
	if (rotational == SMART_UNASSIGNED)
		devattr_rotational(h, &rotational);

	if (!CloseHandle(h)) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return 0; /* assume not rotational */
	}

	if (rotational == SMART_UNASSIGNED)
		return 0; /* assume not rotational */

	return rotational != 0;
}

/**
 * Get POWER state
 */
static int devprobe(uint64_t device, const char* name, const char* smartctl, const char* smartctl_info, int* power, struct smart_attr* smart, uint64_t* info, char* serial, char* family, char* model, char* interf)
{
	char conv_buf[CONV_MAX];
	WCHAR cmd[PATH_MAX + SMART_MAX];
	char file[128];
	FILE* f;
	int ret;
	int count;

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	const char* info_opts = smartctl_info[0] ? smartctl_info : "-a";

	/* if there is a custom smartctl command */
	if (smartctl[0]) {
		char option[SMART_MAX];
		snprintf(option, sizeof(option), smartctl, file);
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -n standby,3 %s %s", windows_exedir(), info_opts, option);
	} else {
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -n standby,3 %s %s", windows_exedir(), info_opts, file);
	}

	count = 0;

retry:
	log_tag("smartctl:%s:%s:run: %s\n", file, name, u16tou8(conv_buf, cmd));

	f = _wpopen(cmd, L"rt");
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from popen).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_attribute(f, file, name, smart, info, serial, family, model, interf) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		log_tag("device:%s:%s:shell\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from pclose).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if first try without custom smartctl command */
	if (count == 0 && smartctl[0] == 0) {
		/*
		 * Handle some common cases in Windows.
		 *
		 * Sometimes the "type" autodetection is wrong, and the command fails at identification
		 * stage, returning with error 2.
		 *
		 * In such conditions we retry using the "sat" type, that often allows to proceed.
		 */
		if (ret == 2) {
			/* retry using the "sat" type */
			snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -n standby,3 %s -d sat %s", windows_exedir(), info_opts, file);

			++count;
			goto retry;
		}
	}

	if (ret == 3) {
		log_tag("attr:%s:%s:power:standby\n", file, name);
		*power = POWER_STANDBY;
	} else {
		log_tag("attr:%s:%s:power:active\n", file, name);
		*power = POWER_ACTIVE;

		/* store the smartctl return value */
		if (smart)
			smart[SMART_FLAGS].raw = ret;
	}

	return 0;
}

/**
 * Spin down a specific device.
 */
static int devdown(uint64_t device, const char* name, const char* smartctl)
{
	char conv_buf[CONV_MAX];
	WCHAR cmd[PATH_MAX + SMART_MAX];
	char file[128];
	FILE* f;
	int ret;
	int count;

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* if there is a custom smartctl command */
	if (smartctl[0]) {
		char option[SMART_MAX];
		snprintf(option, sizeof(option), smartctl, file);
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -s standby,now %s", windows_exedir(), option);
	} else {
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -s standby,now %s", windows_exedir(), file);
	}

	count = 0;

retry:
	log_tag("smartctl:%s:%s:run: %s\n", file, name, u16tou8(conv_buf, cmd));

	f = _wpopen(cmd, L"rt");
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from popen).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_flush(f, file, name) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		log_tag("device:%s:%s:shell\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from pclose).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if first try without custom smartctl command */
	if (count == 0 && smartctl[0] == 0) {
		/*
		 * Handle some common cases in Windows.
		 *
		 * Sometimes the "type" autodetection is wrong, and the command fails at identification
		 * stage, returning with error 2.
		 *
		 * In such conditions we retry using the "sat" type, that often allows to proceed.
		 */
		if (ret == 2) {
			/* retry using the "sat" type */
			snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -s standby,now -d sat %s", windows_exedir(), file);

			++count;
			goto retry;
		}
	}

	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:exit:%d\n", file, name, ret);
		log_fatal(errno, "Failed to run '%s' with return code %xh.\n", u16tou8(conv_buf, cmd), ret);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	log_tag("attr:%s:%s:power:down\n", file, name);

	return 0;
}

/**
 * Spin down a specific device if it's up
 */
static int devdownifup(uint64_t device, const char* name, const char* smartctl, const char* smartctl_info, int* power)
{
	*power = POWER_UNKNOWN;

	if (devprobe(device, name, smartctl, smartctl_info, power, 0, 0, 0, 0, 0, 0) != 0)
		return -1;

	if (*power == POWER_ACTIVE)
		return devdown(device, name, smartctl);

	return 0;
}

/**
 * Spin up a device.
 */
static int devup(uint64_t device, const char* name, const char* wfile)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	BOOL ret;
	DISK_GEOMETRY dg;
	DWORD bytes;
	void* buffer;
	char file[128];

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* open the volume */
	h = CreateFileW(convert(conv_buf, wfile), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, 0);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	bytes = 0;
	ret = DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dg, sizeof(dg), &bytes, NULL);
	if (!ret) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	buffer = VirtualAlloc(NULL, dg.BytesPerSector, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!buffer) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	bytes = 0;
	if (!ReadFile(h, buffer, dg.BytesPerSector, &bytes, NULL)) {
		DWORD error = GetLastError();
		CloseHandle(h);
		VirtualFree(buffer, 0, MEM_RELEASE);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	if (!CloseHandle(h)) {
		DWORD error = GetLastError();
		VirtualFree(buffer, 0, MEM_RELEASE);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	VirtualFree(buffer, 0, MEM_RELEASE);

	log_tag("attr:%s:%s:power:up\n", file, name);

	return 0;
}

/**
 * Thread for spinning up.
 */
static void* thread_spinup(void* arg)
{
	devinfo_t* devinfo = arg;
	uint64_t start;

	/* skip not rotational devices */
	if (devpower(devinfo->device, devinfo->name, devinfo->wfile) == 0)
		return (void*)-1;

	start = os_tick_ms();

	if (devup(devinfo->device, devinfo->name, devinfo->wfile) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spunup device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, os_tick_ms() - start);

	/* after the spin up, get SMART info */
	if (devsmart(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smartctl_info, devinfo->smart, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Retrieve some attributes directly from the system.
	 *
	 * smartctl intentionally skips queries on devices in standby mode
	 * to prevent accidentally spinning them up.
	 */
	devattr(devinfo->device, devinfo->name, devinfo->wfile, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf);

	return 0;
}

/**
 * Thread for spinning down.
 */
static void* thread_spindown(void* arg)
{
	devinfo_t* devinfo = arg;
	uint64_t start;

	/* skip not rotational devices */
	if (devpower(devinfo->device, devinfo->name, devinfo->wfile) == 0)
		return (void*)-1;

	start = os_tick_ms();

	if (devdown(devinfo->device, devinfo->name, devinfo->smartctl) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spundown device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, os_tick_ms() - start);

	return 0;
}

/**
 * Thread for spinning down.
 */
static void* thread_spindownifup(void* arg)
{
	devinfo_t* devinfo = arg;
	uint64_t start;
	int power;

	start = os_tick_ms();

	if (devdownifup(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smartctl_info, &power) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	if (power == POWER_ACTIVE)
		msg_status("Spundown device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, os_tick_ms() - start);

	return 0;
}

/**
 * Thread for getting smart info.
 */
static void* thread_smart(void* arg)
{
	devinfo_t* devinfo = arg;

	if (devsmart(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smartctl_info, devinfo->smart, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Retrieve some attributes directly from the system.
	 *
	 * smartctl intentionally skips queries on devices in standby mode
	 * to prevent accidentally spinning them up.
	 */
	devattr(devinfo->device, devinfo->name, devinfo->wfile, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf);

	return 0;
}

/**
 * Thread for getting power info.
 */
static void* thread_probe(void* arg)
{
	devinfo_t* devinfo = arg;

	if (devprobe(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smartctl_info, &devinfo->power, devinfo->smart, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Retrieve some attributes directly from the system.
	 *
	 * smartctl intentionally skips queries on devices in standby mode
	 * to prevent accidentally spinning them up.
	 */
	devattr(devinfo->device, devinfo->name, devinfo->wfile, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf);

	return 0;
}

static int device_thread(tommy_list* list, void* (*func)(void* arg))
{
	int fail = 0;
	tommy_node* i;

	/* starts all threads */
	for (i = tommy_list_head(list); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;

		thread_create(&devinfo->thread, func, devinfo);
	}

	/* joins all threads */
	for (i = tommy_list_head(list); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		void* retval;

		thread_join(devinfo->thread, &retval);

		if (retval != 0)
			++fail;
	}

	if (fail != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int devquery(tommy_list* high, tommy_list* low, int operation)
{
	tommy_node* i;
	void* (*func)(void* arg) = 0;

	/* for each device */
	for (i = tommy_list_head(high); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		uint64_t access_stat;

		if (devresolve(devinfo->mount, devinfo->file, sizeof(devinfo->file), devinfo->wfile, sizeof(devinfo->wfile)) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(EEXTERNAL, "Failed to resolve path '%s'.\n", devinfo->mount);
			return -1;
			/* LCOV_EXCL_STOP */
		}

		/* retrieve access stat for the high level device */
		if (devstat(devinfo->device, devinfo->name, devinfo->wfile, &access_stat) == 0) {
			/* cumulate access stat in the first split */
			if (devinfo->split)
				devinfo->split->access_stat += access_stat;
			else
				devinfo->access_stat += access_stat;
		}

		/* expand the tree of devices */
		if (devtree(devinfo, low) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(EEXTERNAL, "Failed to expand device '%s'.\n", devinfo->file);
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case DEVICE_UP : func = thread_spinup; break;
	case DEVICE_DOWN : func = thread_spindown; break;
	case DEVICE_SMART : func = thread_smart; break;
	case DEVICE_PROBE : func = thread_probe; break;
	case DEVICE_DOWNIFUP : func = thread_spindownifup; break;
	}

	if (!func)
		return 0;

	return device_thread(low, func);
}

int devmap(void)
{
	for (int i = 0; i < 32; i++) {
		char wfile[PATH_MAX];
		wchar_t conv_buf[CONV_MAX];
		pathprint(wfile, sizeof(wfile), "\\\\.\\PhysicalDrive%d", i);
		HANDLE h;

		/* open the volume */
		h = CreateFileW(convert(conv_buf, wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
		if (h == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();
			if (error != ERROR_FILE_NOT_FOUND)
				log_tag("enumeration:/dev/pd%d::error:%lu\n", i, error);
			continue;
		}

		char serial[SMART_MAX];
		serial[0] = 0;
		devattr_property(h, serial, 0, 0);
		if (serial[0]) {
			log_tag("map:/dev/pd%d:%s\n", i, esc_tag(serial));
		}

		CloseHandle(h);
	}

	return 0;
}

/****************************************************************************/
/* temperature */

int ambient_temperature(void)
{
	return 0;
}

/****************************************************************************/
/* app */

void app_init(void)
{
	/*
	 * Set LC_ALL=C to make child ignoring the locale when printing info
	 *
	 * Note anyway that in Windows this trick doesn't work because the
	 * Windows locale is used with priority.
	 */
	_wputenv_s(L"LC_ALL", L"C");
}

void app_done(void)
{
}

void app_default_conf(char* conf, size_t conf_size, const char* argv0)
{
	char* slash;

	pathimport(conf, conf_size, argv0);

	slash = strrchr(conf, '/');
	if (slash) {
		slash[1] = 0;
		pathcat(conf, conf_size, PACKAGE ".conf");
	} else {
		pathcpy(conf, conf_size, PACKAGE ".conf");
	}
}

#endif

