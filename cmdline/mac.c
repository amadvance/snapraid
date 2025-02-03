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

#ifdef __APPLE__

#include "mac.h"
#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>

int devuuid_macos(char *path, char* uuid, size_t uuid_size) {
	CFStringRef path_apple = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
	DASessionRef session = DASessionCreate(kCFAllocatorDefault);

	CFURLRef path_appler = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path_apple, kCFURLPOSIXPathStyle, false);
	DADiskRef disk;
	do {
		disk = DADiskCreateFromVolumePath(kCFAllocatorDefault, session, path_appler);
		if (disk) {
			CFRelease(path_appler);
			break;
		} else {
			CFURLRef parent_path_appler = CFURLCreateCopyDeletingLastPathComponent(kCFAllocatorDefault, path_appler);
			CFRelease(path_appler);
			path_appler = parent_path_appler;
		}
	} while (true); // This is guaranteed to succeed eventually because it'll hit `/`.
	
	CFDictionaryRef description = DADiskCopyDescription(disk);
	CFUUIDRef uuid_apple = CFDictionaryGetValue(description, kDADiskDescriptionVolumeUUIDKey);
	CFStringRef uuid_string = CFUUIDCreateString(kCFAllocatorDefault, uuid_apple);
	bool success = CFStringGetCString(uuid_string, uuid, uuid_size, kCFStringEncodingUTF8);
	CFRelease(uuid_string);
	CFRelease(description);
	CFRelease(disk);
	CFRelease(session);
	CFRelease(path_apple);
	if (success) {
		return 0;
	} else {
		return 1;
	}
}

#endif
