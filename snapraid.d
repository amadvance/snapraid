Name{number}
	snapraid - SnapRAID Backup for Disk Arrays

Synopsis
	:snapraid [-c, --conf CONFIG]
	:	[-f, --filter PATTERN] [-d, --filter-disk NAME]
	:	[-m, --filter-missing] [-e, --filter-error]
	:	[-a, --audit-only] [-h, --pre-hash] [-i, --import DIR]
	:	[-p, --plan PERC|bad|new|full]
	:	[-o, --older-than DAYS] [-l, --log FILE]
	:	[-s, --spin-down-on-error] [-w, --bw-limit RATE]
	:	[-Z, --force-zero] [-E, --force-empty]
	:	[-U, --force-uuid] [-D, --force-device]
	:	[-N, --force-nocopy] [-F, --force-full]
	:	[-R, --force-realloc]
	:	[-S, --start BLKSTART] [-B, --count BLKCOUNT]
	:	[-L, --error-limit NUMBER]
	:	[-A, --stats]
	:	[-v, --verbose] [-q, --quiet]
	:	status|smart|probe|up|down|diff|sync|scrub|fix|check
	:	|list|dup|pool|devices|touch|rehash

	:snapraid [-V, --version] [-H, --help] [-C, --gen-conf CONTENT]

Description
	SnapRAID is a backup program designed for disk arrays, storing
	parity information for data recovery in the event of up to six
	disk failures.

	Primarily intended for home media centers with large,
	infrequently changing files, SnapRAID offers several features:

	* You can utilize disks already filled with files without the
		need to reformat them, accessing them as usual.
	* All your data is hashed to ensure data integrity and prevent
		silent corruption.
	* When the number of failed disks exceeds the parity count,
		data loss is confined to the affected disks; data on
		other disks remains accessible.
	* If you accidentally delete files on a disk, recovery is
		possible.
	* Disks can have different sizes.
	* You can add disks at any time.
	* SnapRAID doesn't lock in your data; you can stop using it
		anytime without reformatting or moving data.
	* To access a file, only a single disk needs to spin, saving
		power and reducing noise.

	For more information, please visit the official SnapRAID site:

		:http://www.snapraid.it/

Limitations
	SnapRAID is a hybrid between a RAID and a backup program, aiming to combine
	the best benefits of both. However, it has some limitations that you should
	consider before using it.

	The main limitation is that if a disk fails and you haven't recently synced,
	you may not be able to fully recover.
	More specifically, you may be unable to recover up to the size of
	the changed or deleted files since the last sync operation.
	This occurs even if the changed or deleted files are not on the
	failed disk. This is why SnapRAID is better suited for
	data that rarely changes.

	On the other hand, newly added files don't prevent recovery of already
	existing files. You will only lose the recently added files if they
	are on the failed disk.

	Other SnapRAID limitations are:

	* With SnapRAID, you still have separate file systems for each disk.
		With RAID, you get a single large file system.
	* SnapRAID doesn't stripe data.
		With RAID, you get a speed boost with striping.
	* SnapRAID doesn't support real-time recovery.
		With RAID, you do not have to stop working when a disk fails.
	* SnapRAID can recover data only from a limited number of disk failures.
		With a backup, you can recover from a complete
		failure of the entire disk array.
	* Only file names, timestamps, symlinks, and hardlinks are saved.
		Permissions, ownership, and extended attributes are not saved.

Getting Started
	To use SnapRAID, you need to first select one disk in your disk array
	to dedicate to "parity" information. With one disk for parity, you
	will be able to recover from a single disk failure, similar to RAID5.

	If you want to recover from more disk failures, similar to RAID6,
	you must reserve additional disks for parity. Each additional parity
	disk allows recovery from one more disk failure.

	As parity disks, you must pick the largest disks in the array,
	as the parity information may grow to the size of the largest data
	disk in the array.

	These disks will be dedicated to storing the "parity" files.
	You should not store your data on them.

	Then, you must define the "data" disks that you want to protect
	with SnapRAID. The protection is more effective if these disks
	contain data that rarely changes. For this reason, it's better to
	NOT include the Windows C:\ disk or the Unix /home, /var, and /tmp
	directories.

	The list of files is saved in the "content" files, usually
	stored on the data, parity, or boot disks.
	This file contains the details of your backup, including all the
	checksums to verify its integrity.
	The "content" file is stored in multiple copies, and each copy must
	be on a different disk to ensure that, even in case of multiple
	disk failures, at least one copy is available.

	For example, suppose you are interested in only one parity level
	of protection, and your disks are located at:

		:/mnt/diskp <- selected disk for parity
		:/mnt/disk1 <- first disk to protect
		:/mnt/disk2 <- second disk to protect
		:/mnt/disk3 <- third disk to protect

	You must create the configuration file /etc/snapraid.conf with
	the following options:

		:parity /mnt/diskp/snapraid.parity
		:content /var/snapraid/snapraid.content
		:content /mnt/disk1/snapraid.content
		:content /mnt/disk2/snapraid.content
		:data d1 /mnt/disk1/
		:data d2 /mnt/disk2/
		:data d3 /mnt/disk3/

	If you are on Windows, you should use the Windows path format, with drive
	letters and backslashes instead of slashes.

		:parity E:\snapraid.parity
		:content C:\snapraid\snapraid.content
		:content F:\array\snapraid.content
		:content G:\array\snapraid.content
		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\

	If you have many disks and run out of drive letters, you can mount
	disks directly in subfolders. See:

		:https://www.google.com/search?q=Windows+mount+point

	At this point, you are ready to run the "sync" command to build the
	parity information.

		:snapraid sync

	This process may take several hours the first time, depending on the size
	of the data already present on the disks. If the disks are empty,
	the process is immediate.

	You can stop it at any time by pressing Ctrl+C, and at the next run, it
	will resume where it was interrupted.

	When this command completes, your data is SAFE.

	Now you can start using your array as you like and periodically
	update the parity information by running the "sync" command.

  Scrubbing
	To periodically check the data and parity for errors, you can
	run the "scrub" command.

		:snapraid scrub

	This command compares the data in your array with the hash computed
	during the "sync" command to verify integrity.

	Each run of the command checks approximately 8% of the array, excluding data
	already scrubbed in the previous 10 days.
	You can use the -p, --plan option to specify a different amount
	and the -o, --older-than option to specify a different age in days.
	For example, to check 5% of the array for blocks older than 20 days, use:

		:snapraid -p 5 -o 20 scrub  

	If silent or input/output errors are found during the process,
	the corresponding blocks are marked as bad in the "content" file
	and listed in the "status" command.

		:snapraid status

	To fix them, you can use the "fix" command, filtering for bad blocks with
	the -e, --filter-error option:

		:snapraid -e fix

	At the next "scrub," the errors will disappear from the "status" report
	if they are truly fixed. To make it faster, you can use -p bad to scrub
	only blocks marked as bad.

		:snapraid -p bad scrub

	Running "scrub" on an unsynced array may report errors caused by
	removed or modified files. These errors are reported in the "scrub"
	output, but the related blocks are not marked as bad.

  Pooling
	Note: The pooling feature described below has been superseded by the
	mergefs tool, which is now the recommended option for Linux users in
	the SnapRAID community. Mergefs provides a more flexible and efficient
	way to pool multiple drives into a single unified mount point,
	allowing seamless access to files across your array without relying
	on symbolic links. It integrates well with SnapRAID for parity
	protection and is commonly used in setups like OpenMediaVault (OMV)
	or custom NAS configurations.
  
	To have all the files in your array shown in the same directory tree,
	you can enable the "pooling" feature. It creates a read-only virtual
	view of all the files in your array using symbolic links.

	You can configure the "pooling" directory in the configuration file with:

		:pool /pool

	or, if you are on Windows, with:

		:pool C:\pool

	and then run the "pool" command to create or update the virtual view.

		:snapraid pool

	If you are using a Unix platform and want to share this directory
	over the network to either Windows or Unix machines, you should add
	the following options to your /etc/samba/smb.conf:

		:# In the global section of smb.conf
		:unix extensions = no

		:# In the share section of smb.conf
		:[pool]
		:comment = Pool
		:path = /pool
		:read only = yes
		:guest ok = yes
		:wide links = yes
		:follow symlinks = yes

	In Windows, sharing symbolic links over a network requires clients to
	resolve them remotely. To enable this, besides sharing the pool directory,
	you must also share all the disks independently, using the disk names
	defined in the configuration file as share points. You must also specify
	in the "share" option of the configuration file the Windows UNC path that
	remote clients need to use to access these shared disks.

	For example, operating from a server named "darkstar", you can use
	the options:

		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\
		:pool C:\pool
		:share \\darkstar

	and share the following directories over the network:

		:\\darkstar\pool -> C:\pool
		:\\darkstar\d1 -> F:\array
		:\\darkstar\d2 -> G:\array
		:\\darkstar\d3 -> H:\array

	to allow remote clients to access all the files at \\darkstar\pool.

	You may also need to configure remote clients to enable access to remote
	symlinks with the command:

		:fsutil behavior set SymlinkEvaluation L2L:1 R2R:1 L2R:1 R2L:1

  Undeleting
	SnapRAID functions more like a backup program than a RAID system, and it
	can be used to restore or undelete files to their previous state using
	the -f, --filter option:

		:snapraid fix -f FILE

	or for a directory:

		:snapraid fix -f DIR/

	You can also use it to recover only accidentally deleted files inside
	a directory using the -m, --filter-missing option, which restores
	only missing files, leaving all others untouched.

		:snapraid fix -m -f DIR/

	Or to recover all the deleted files on all drives with:

		:snapraid fix -m

  Recovering
	The worst has happened, and you have lost one or more disks!

	DO NOT PANIC! You will be able to recover them!

	The first thing you must do is avoid further changes to your disk array.
	Disable any remote connections to it and any scheduled processes, including
	any scheduled SnapRAID nightly sync or scrub.

	Then proceed with the following steps.

    STEP 1 -> Reconfigure
	You need some space to recover, ideally on additional
	spare disks, but an external USB disk or remote disk will suffice.

	Modify the SnapRAID configuration file to make the "data" or "parity"
	option of the failed disk point to a location with enough empty
	space to recover the files.

	For example, if disk "d1" has failed, change from:

		:data d1 /mnt/disk1/

	to:

		:data d1 /mnt/new_spare_disk/

	If the disk to recover is a parity disk, update the appropriate "parity"
	option.
	If you have multiple failed disks, update all their configuration options.

    STEP 2 -> Fix
	Run the fix command, storing the log in an external file with:

		:snapraid -d NAME -l fix.log fix

	Where NAME is the name of the disk, such as "d1" in our previous example.
	If the disk to recover is a parity disk, use the names "parity", "2-parity",
	etc.
	If you have multiple failed disks, use multiple -d options to specify all
	of them.

	This command will take a long time.

	Ensure you have a few gigabytes free to store the fix.log file.
	Run it from a disk with sufficient free space.

	Now you have recovered all that is recoverable. If some files are partially
	or totally unrecoverable, they will be renamed by adding the ".unrecoverable"
	extension.

	You can find a detailed list of all unrecoverable blocks in the fix.log file
	by checking all lines starting with "unrecoverable:".

	If you are not satisfied with the recovery, you can retry it as many
	times as you wish.

	For example, if you have removed files from the array after the last
	"sync", this may result in some files not being recovered.
	In this case, you can retry the "fix" using the -i, --import option,
	specifying where these files are now to include them again in the
	recovery process.

	If you are satisfied with the recovery, you can proceed further,
	but note that after syncing, you cannot retry the "fix" command
	anymore!

    STEP 3 -> Check
	As a cautious check, you can now run a "check" command to ensure that
	everything is correct on the recovered disk.

		:snapraid -d NAME -a check

	Where NAME is the name of the disk, such as "d1" in our previous example.

	The -d and -a options tell SnapRAID to check only the specified disk
	and ignore all parity data.

	This command will take a long time, but if you are not overly cautious,
	you can skip it.

    STEP 4 -> Sync
	Run the "sync" command to resynchronize the array with the new disk.

		:snapraid sync

	If everything is recovered, this command is immediate.

Commands
	SnapRAID provides a few simple commands that allow you to:

	* Print the status of the array -> "status"
	* Control the disks -> "smart", "probe", "up", "down"
	* Make a backup/snapshot -> "sync"
	* Periodically check data -> "scrub"
	* Restore the last backup/snapshot -> "fix".

	Commands must be written in lowercase.

  status
	Prints a summary of the state of the disk array.

	It includes information about parity fragmentation, how old
	the blocks are without checking, and all recorded silent
	errors encountered while scrubbing.

	The information presented refers to the latest time you
	ran "sync". Later modifications are not taken into account.

	If bad blocks were detected, their block numbers are listed.
	To fix them, you can use the "fix -e" command.

	It also shows a graph representing the last time each block
	was scrubbed or synced. Scrubbed blocks are shown with '*',
	blocks synced but not yet scrubbed with 'o'.

	Nothing is modified.

  smart
	Prints a SMART report of all the disks in the system.

	It includes an estimation of the probability of failure in the next
	year, allowing you to plan maintenance replacements of disks that show
	suspicious attributes.

	This probability estimation is obtained by correlating the SMART attributes
	of the disks with the Backblaze data available at:

		:https://www.backblaze.com/hard-drive-test-data.html

	If SMART reports that a disk is failing, "FAIL" or "PREFAIL" is printed
	for that disk, and SnapRAID returns with an error.
	In this case, immediate replacement of the disk is highly recommended.

	Other possible status strings are:
		logfail - In the past, some attributes were lower than
			the threshold.
		logerr - The device error log contains errors.
		selferr - The device self-test log contains errors.

	If the -v, --verbose option is specified, a deeper statistical analysis
	is provided. This analysis can help you decide if you need more
	or less parity.

	This command uses the "smartctl" tool and is equivalent to running
	"smartctl -a" on all devices.

	If your devices are not auto-detected correctly, you can specify
	a custom command using the "smartctl" option in the configuration
	file.

	Nothing is modified.

  probe
	Prints the POWER state of all disks in the system.

	"Standby" means the disk is not spinning. "Active" means
	the disk is spinning.

	This command uses the "smartctl" tool and is equivalent to running
	"smartctl -n standby -i" on all devices.

	If your devices are not auto-detected correctly, you can specify
	a custom command using the "smartctl" option in the configuration
	file.

	Nothing is modified.

  up
	Spins up all the disks of the array.

	You can spin up only specific disks using the -d, --filter-disk option.

	Spinning up all the disks at the same time requires a lot of power.
	Ensure that your power supply can sustain it.

	Nothing is modified.

  down
	Spins down all the disks of the array.

	This command uses the "smartctl" tool and is equivalent to running
	"smartctl -s standby,now" on all devices.

	You can spin down only specific disks using the -d, --filter-disk
	option.

	To automatically spin down on error, you can use the -s, --spin-down-on-error
	option with any other command, which is equivalent to running "down" manually
	when an error occurs.

	Nothing is modified.

  diff
	Lists all the files modified since the last "sync" that need to have
	their parity data recomputed.

	This command doesn't check the file data, but only the file timestamp,
	size, and inode.

	After listing all changed files, a summary of the changes is
	presented, grouped by:
		equal - Files unchanged from before.
		added - Files added that were not present before.
		removed - Files removed.
		updated - Files with a different size or timestamp, meaning they
			were modified.
		moved - Files moved to a different directory on the same disk.
			They are identified by having the same name, size, timestamp,
			and inode, but a different directory.
		copied - Files copied on the same or a different disk. Note that if
			they are truly moved to a different disk, they will also be
			counted in "removed".
			They are identified by having the same name, size, and
			timestamp. If the sub-second timestamp is zero,
			the full path must match, not just the name.
		restored - Files with a different inode but matching name, size, and timestamp.
			These are usually files restored after being deleted.

	If a "sync" is required, the process return code is 2, instead of the
	default 0. The return code 1 is used for a generic error condition.

	Nothing is modified.

  sync
	Updates the parity information. All modified files
	in the disk array are read, and the corresponding parity
	data is updated.

	You can stop this process at any time by pressing Ctrl+C,
	without losing the work already done.
	At the next run, the "sync" process will resume where
	it was interrupted.

	If silent or input/output errors are found during the process,
	the corresponding blocks are marked as bad.

	Files are identified by path and/or inode and checked by
	size and timestamp.
	If the file size or timestamp differs, the parity data
	is recomputed for the entire file.
	If the file is moved or renamed on the same disk, keeping the
	same inode, the parity is not recomputed.
	If the file is moved to another disk, the parity is recomputed,
	but the previously computed hash information is retained.

	The "content" and "parity" files are modified if necessary.
	The files in the array are NOT modified.

  scrub
	Scrubs the array, checking for silent or input/output errors in data
	and parity disks.

	Each invocation checks approximately 8% of the array, excluding
	data already scrubbed in the last 10 days.
	This means that scrubbing once a week ensures every bit of data is checked
	at least once every three months.

	You can define a different scrub plan or amount using the -p, --plan
	option, which accepts:
	bad - Scrub blocks marked bad.
	new - Scrub just-synced blocks not yet scrubbed.
	full - Scrub everything.
	0-100 - Scrub the specified percentage of blocks.

	If you specify a percentage amount, you can also use the -o, --older-than
	option to define how old the block should be.
	The oldest blocks are scrubbed first, ensuring an optimal check.
	If you want to scrub only the just-synced blocks not yet scrubbed,
	use the "-p new" option.

	To get details of the scrub status, use the "status" command.

	For any silent or input/output error found, the corresponding blocks
	are marked as bad in the "content" file.
	These bad blocks are listed in "status" and can be fixed with "fix -e".
	After the fix, at the next scrub, they will be rechecked, and if found
	corrected, the bad mark will be removed.
	To scrub only the bad blocks, you can use the "scrub -p bad" command.

	It's recommended to run "scrub" only on a synced array to avoid
	reported errors caused by unsynced data. These errors are recognized
	as not being silent errors, and the blocks are not marked as bad,
	but such errors are reported in the output of the command.

	The "content" file is modified to update the time of the last check
	for each block and to mark bad blocks.
	The "parity" files are NOT modified.
	The files in the array are NOT modified.

  fix
	Fixes all the files and the parity data.

	All files and parity data are compared with the snapshot
	state saved in the last "sync".
	If a difference is found, it is reverted to the stored snapshot.

	WARNING! The "fix" command does not differentiate between errors and
	intentional modifications. It unconditionally reverts the file state
	to the last "sync".

	If no other option is specified, the entire array is processed.
	Use the filter options to select a subset of files or disks to operate on.

	To fix only the blocks marked bad during "sync" and "scrub",
	use the -e, --filter-error option.
	Unlike other filter options, this one applies fixes only to files that are
	unchanged since the latest "sync".

	SnapRAID renames all files that cannot be fixed by adding the
	".unrecoverable" extension.

	Before fixing, the entire array is scanned to find any files moved
	since the last "sync" operation.
	These files are identified by their timestamp, ignoring their name
	and directory, and are used in the recovery process if necessary.
	If you moved some of them outside the array, you can use the -i, --import
	option to specify additional directories to scan.

	Files are identified only by path, not by inode.

	The "content" file is NOT modified.
	The "parity" files are modified if necessary.
	The files in the array are modified if necessary.

  check
	Verifies all the files and the parity data.

	It works like "fix", but it only simulates a recovery and no changes
	are written to the array.

	This command is primarily intended for manual verification,
	such as after a recovery process or in other special conditions.
	For periodic and scheduled checks, use "scrub".

	If you use the -a, --audit-only option, only the file
	data is checked, and the parity data is ignored for a
	faster run.

	Files are identified only by path, not by inode.

	Nothing is modified.

  list
	Lists all the files contained in the array at the time of the
	last "sync".

	With -v or --verbose, the subsecond time is also shown.

	Nothing is modified.

  dup
	Lists all duplicate files. Two files are assumed equal if their
	hashes match. The file data is not read; only the
	precomputed hashes are used.

	Nothing is modified.

  pool
	Creates or updates a virtual view of all
	the files in your disk array in the "pooling" directory.

	The files are not copied but linked using
	symbolic links.

	When updating, all existing symbolic links and empty
	subdirectories are deleted and replaced with the new
	view of the array. Any other regular files are left in place.

	Nothing is modified outside the pool directory.

  devices
	Prints the low-level devices used by the array.

	This command displays the device associations in the array
	and is mainly intended as a script interface.

	The first two columns are the low-level device ID and path.
	The next two columns are the high-level device ID and path.
	The last column is the disk name in the array.

	In most cases, you have one low-level device for each disk in the
	array, but in some more complex configurations, you may have multiple
	low-level devices used by a single disk in the array.

	Nothing is modified.

  touch
	Sets an arbitrary sub-second timestamp for all files
	that have it set to zero.

	This improves SnapRAID's ability to recognize moved
	and copied files, as it makes the timestamp almost unique,
	reducing possible duplicates.

	More specifically, if the sub-second timestamp is not zero,
	a moved or copied file is identified as such if it matches
	the name, size, and timestamp. If the sub-second timestamp
	is zero, it is considered a copy only if the full path,
	size, and timestamp all match.

	The second-precision timestamp is not modified,
	so all the dates and times of your files will be preserved.

  rehash
	Schedules a rehash of the entire array.

	This command changes the hash kind used, typically when upgrading
	from a 32-bit system to a 64-bit one, to switch from
	MurmurHash3 to the faster SpookyHash.

	If you are already using the optimal hash, this command
	does nothing and informs you that no action is needed.

	The rehash is not performed immediately but takes place
	progressively during "sync" and "scrub".

	You can check the rehash state using "status".

	During the rehash, SnapRAID maintains full functionality,
	with the only exception that "dup" cannot detect duplicated
	files using a different hash.

Options
	SnapRAID provides the following options:

	-c, --conf CONFIG
		Selects the configuration file to use. If not specified, in Unix
		it uses the file "/usr/local/etc/snapraid.conf" if it exists,
		otherwise "/etc/snapraid.conf".
		In Windows, it uses the file "snapraid.conf" in the same
		directory as "snapraid.exe".

	-f, --filter PATTERN
		Filters the files to process in "check" and "fix".
		Only the files matching the specified pattern are processed.
		This option can be used multiple times.
		See the PATTERN section for more details on
		pattern specifications.
		In Unix, ensure globbing characters are quoted if used.
		This option can be used only with "check" and "fix".
		It cannot be used with "sync" and "scrub", as they always
		process the entire array.

	-d, --filter-disk NAME
		Filters the disks to process in "check", "fix", "up", and "down".
		You must specify a disk name as defined in the configuration
		file.
		You can also specify parity disks with the names: "parity", "2-parity",
		"3-parity", etc., to limit operations to a specific parity disk.
		If you combine multiple --filter, --filter-disk, and --filter-missing options,
		only files matching all the filters are selected.
		This option can be used multiple times.
		This option can be used only with "check", "fix", "up", and "down".
		It cannot be used with "sync" and "scrub", as they always
		process the entire array.

	-m, --filter-missing
		Filters the files to process in "check" and "fix".
		Only the files missing or deleted from the array are processed.
		When used with "fix", this acts as an "undelete" command.
		If you combine multiple --filter, --filter-disk, and --filter-missing options,
		only files matching all the filters are selected.
		This option can be used only with "check" and "fix".
		It cannot be used with "sync" and "scrub", as they always
		process the entire array.

	-e, --filter-error
		Processes the files with errors in "check" and "fix".
		It processes only files that have blocks marked with silent
		or input/output errors during "sync" and "scrub", as listed in "status".
		This option can be used only with "check" and "fix".

	-p, --plan PERC|bad|new|full
		Selects the scrub plan. If PERC is a numeric value from 0 to 100,
		it is interpreted as the percentage of blocks to scrub.
		Instead of a percentage, you can specify a plan:
		"bad" scrubs bad blocks, "new" scrubs blocks not yet scrubbed,
		and "full" scrubs everything.
		This option can be used only with "scrub".

	-o, --older-than DAYS
		Selects the oldest part of the array to process in "scrub".
		DAYS is the minimum age in days for a block to be scrubbed;
		the default is 10.
		Blocks marked as bad are always scrubbed regardless of this option.
		This option can be used only with "scrub".

	-a, --audit-only
		In "check", verifies the hash of the files without
		checking the parity data.
		If you are interested only in checking the file data, this
		option can significantly speed up the checking process.
		This option can be used only with "check".

	-h, --pre-hash
		In "sync", runs a preliminary hashing phase of all new data
		for additional verification before the parity computation.
		Usually, in "sync", no preliminary hashing is done, and the new
		data is hashed just before the parity computation when it is read
		for the first time.
		This process occurs when the system is under
		heavy load, with all disks spinning and a busy CPU.
		This is an extreme condition for the machine, and if it has a
		latent hardware problem, silent errors may go undetected
		because the data is not yet hashed.
		To avoid this risk, you can enable the "pre-hash" mode to have
		all the data read twice to ensure its integrity.
		This option also verifies files moved within the array
		to ensure the move operation was successful and, if necessary,
		allows you to run a fix operation before proceeding.
		This option can be used only with "sync".

	-i, --import DIR
		Imports from the specified directory any files deleted
		from the array after the last "sync".
		If you still have such files, they can be used by "check"
		and "fix" to improve the recovery process.
		The files are read, including in subdirectories, and are
		identified regardless of their name.
		This option can be used only with "check" and "fix".

	-s, --spin-down-on-error
		On any error, spins down all managed disks before exiting with
		a non-zero status code. This prevents the drives from
		remaining active and spinning after an aborted operation,
		helping to avoid unnecessary heat buildup and power
		consumption. Use this option to ensure disks are safely
		stopped even when a command fails.

	-w, --bw-limit RATE
		Applies a global bandwidth limit for all disks. The RATE is
		the number of bytes per second. You can specify a multiplier
		such as K, M, or G (e.g., --bw-limit 1G).

	-A, --stats
		Enables an extended status view that shows additional information.

		The screen displays two graphs:

		The first graph shows the number of buffered stripes for each
		disk, along with the file path of the file currently being
		accessed on that disk. Typically, the slowest disk will have
		no buffer available, which determines the maximum achievable
		bandwidth.

		The second graph shows the percentage of time spent waiting
		over the past 100 seconds. The slowest disk is expected to
		cause most of the wait time, while other disks should have
		little or no wait time because they can use their buffered stripes.
		This graph also shows the time spent waiting for hash
		calculations and RAID computations.

		All computations run in parallel with disk operations.
		Therefore, as long as there is measurable wait time for at
		least one disk, it indicates that the CPU is fast enough to
		keep up with the workload.

	-Z, --force-zero
		Forces the insecure operation of syncing a file with zero
		size that was previously non-zero.
		If SnapRAID detects such a condition, it stops proceeding
		unless you specify this option.
		This allows you to easily detect when, after a system crash,
		some accessed files were truncated.
		This is a possible condition in Linux with the ext3/ext4
		file systems.
		This option can be used only with "sync".

	-E, --force-empty
		Forces the insecure operation of syncing a disk with all
		the original files missing.
		If SnapRAID detects that all the files originally present
		on the disk are missing or rewritten, it stops proceeding
		unless you specify this option.
		This allows you to easily detect when a data file system is not
		mounted.
		This option can be used only with "sync".

	-U, --force-uuid
		Forces the insecure operation of syncing, checking, and fixing
		with disks that have changed their UUID.
		If SnapRAID detects that some disks have changed UUID,
		it stops proceeding unless you specify this option.
		This allows you to detect when your disks are mounted at the
		wrong mount points.
		It is, however, allowed to have a single UUID change with
		single parity, and more with multiple parity, because this is
		the normal case when replacing disks after a recovery.
		This option can be used only with "sync", "check", or
		"fix".

	-D, --force-device
		Forces the insecure operation of fixing with inaccessible disks
		or with disks on the same physical device.
		For example, if you lost two data disks and have a spare disk to recover
		only the first one, you can ignore the second inaccessible disk.
		Or, if you want to recover a disk in the free space left on an
		already used disk, sharing the same physical device.
		This option can be used only with "fix".

	-N, --force-nocopy
		In "sync", "check", and "fix", disables the copy detection heuristic.
		Without this option, SnapRAID assumes that files with the same
		attributes, such as name, size, and timestamp, are copies with the
		same data.
		This allows identification of copied or moved files from one disk
		to another and reuses the already computed hash information
		to detect silent errors or to recover missing files.
		In some rare cases, this behavior may result in false positives
		or a slow process due to many hash verifications, and this
		option allows you to resolve such issues.
		This option can be used only with "sync", "check", and "fix".

	-F, --force-full
		In "sync", forces a full recomputation of the parity.
		This option can be used when you add a new parity level or if
		you reverted to an old content file using more recent parity data.
		Instead of recreating the parity from scratch, this allows
		you to reuse the hashes present in the content file to validate data
		and maintain data protection during the "sync" process using
		the existing parity data.
		This option can be used only with "sync".

	-R, --force-realloc
		In "sync", forces a full reallocation of files and rebuild of the parity.
		This option can be used to completely reallocate all files,
		removing fragmentation, while reusing the hashes present in the content
		file to validate data.
		This option can be used only with "sync".
		WARNING! This option is for experts only, and it is highly
		recommended not to use it.
		You DO NOT have data protection during the "sync" operation.

	-l, --log FILE
		Writes a detailed log to the specified file.
		If this option is not specified, unexpected errors are printed
		to the screen, potentially resulting in excessive output in case of
		many errors. When -l, --log is specified, only
		fatal errors that cause SnapRAID to stop are printed
		to the screen.
		If the path starts with '>>', the file is opened
		in append mode. Occurrences of '%D' and '%T' in the name are
		replaced with the date and time in the format YYYYMMDD and
		HHMMSS. In Windows batch files, you must double
		the '%' character, e.g., result-%%D.log. To use '>>', you must
		enclose the name in quotes, e.g., ">>result.log".
		To output the log to standard output or standard error,
		you can use ">&1" and ">&2", respectively.

	-L, --error-limit NUMBER
		Sets a new error limit before stopping execution.
		By default, SnapRAID stops if it encounters more than 100
		input/output errors, indicating that a disk is likely failing.
		This option affects "sync" and "scrub", which are allowed
		to continue after the first set of disk errors to try
		to complete their operations.
		However, "check" and "fix" always stop at the first error.

	-S, --start BLKSTART
		Starts processing from the specified
		block number. This can be useful for retrying to check
		or fix specific blocks in case of a damaged disk.
		This option is mainly for advanced manual recovery.

	-B, --count BLKCOUNT
		Processes only the specified number of blocks.
		This option is mainly for advanced manual recovery.

	-C, --gen-conf CONTENT
		Generates a dummy configuration file from an existing
		content file.
		The configuration file is written to standard output
		and does not overwrite an existing one.
		This configuration file also contains the information
		needed to reconstruct the disk mount points in case you
		lose the entire system.

	-v, --verbose
		Prints more information to the screen.
		If specified once, it prints excluded files
		and additional statistics.
		This option has no effect on the log files.

	-q, --quiet
		Prints less information to the screen.
		If specified once, it removes the progress bar; twice,
		the running operations; three times, the info
		messages; four times, the status messages.
		Fatal errors are always printed to the screen.
		This option has no effect on the log files.

	-H, --help
		Prints a short help screen.

	-V, --version
		Prints the program version.

Configuration
	SnapRAID requires a configuration file to know where your disk array
	is located and where to store the parity information.

	In Unix, it uses the file "/usr/local/etc/snapraid.conf" if it exists,
	otherwise "/etc/snapraid.conf".
	In Windows, it uses the file "snapraid.conf" in the same
	directory as "snapraid.exe".

	It must contain the following options (case-sensitive):

  parity FILE [,FILE] ...
	Defines the files to use to store the parity information.
	The parity enables protection from a single disk
	failure, similar to RAID5.

	You can specify multiple files, which must be on different disks.
	When a file cannot grow anymore, the next one is used.
	The total space available must be at least as large as the largest data disk in
	the array.

	You can add additional parity files later, but you
	cannot reorder or remove them.

	Keeping the parity disks reserved for parity ensures that
	they do not become fragmented, improving performance.

	In Windows, 256 MB is left unused on each disk to avoid the
	warning about full disks.

	This option is mandatory and can be used only once.

  (2,3,4,5,6)-parity FILE [,FILE] ...
	Defines the files to use to store extra parity information.

	For each parity level specified, one additional level of protection
	is enabled:

	* 2-parity enables RAID6 dual parity.
	* 3-parity enables triple parity.
	* 4-parity enables quad (four) parity.
	* 5-parity enables penta (five) parity.
	* 6-parity enables hexa (six) parity.

	Each parity level requires the presence of all previous parity
	levels.

	The same considerations as for the 'parity' option apply.

	These options are optional and can be used only once.

  z-parity FILE [,FILE] ...
	Defines an alternate file and format to store triple parity.

	This option is an alternative to '3-parity', primarily intended for
	low-end CPUs like ARM or AMD Phenom, Athlon, and Opteron that do not
	support the SSSE3 instruction set. In such cases, it provides
	better performance.

	This format is similar to but faster than the one used by ZFS RAIDZ3.
	Like ZFS, it does not work beyond triple parity.

	When using '3-parity', you will be warned if it is recommended to use
	the 'z-parity' format for performance improvement.

	It is possible to convert from one format to another by adjusting
	the configuration file with the desired z-parity or 3-parity file
	and using 'fix' to recreate it.

  content FILE
	Defines the file to use to store the list and checksums of all the
	files present in your disk array.

	It can be placed on a disk used for data, parity, or
	any other disk available.
	If you use a data disk, this file is automatically excluded
	from the "sync" process.

	This option is mandatory and can be used multiple times to save
	multiple copies of the same file.

	You must store at least one copy for each parity disk used
	plus one. Using additional copies does not hurt.

  data NAME DIR
	Defines the name and mount point of the data disks in
	the array. NAME is used to identify the disk and must
	be unique. DIR is the mount point of the disk in the
	file system.

	You can change the mount point as needed, as long as
	you keep the NAME fixed.

	You should use one option for each data disk in the array.

	You can rename a disk later by changing the NAME directly
	in the configuration file and then running a 'sync' command.
	In the case of renaming, the association is done using the stored
	UUID of the disks.

  nohidden
	Excludes all hidden files and directories.
	In Unix, hidden files are those starting with ".".
	In Windows, they are those with the hidden attribute.

  exclude/include PATTERN
	Defines the file or directory patterns to exclude or include
	in the sync process.
	All patterns are processed in the specified order.

	If the first pattern that matches is an "exclude" one, the file
	is excluded. If it is an "include" one, the file is included.
	If no pattern matches, the file is excluded if the last pattern
	specified is an "include", or included if the last pattern
	specified is an "exclude".

	See the PATTERN section for more details on pattern
	specifications.

	This option can be used multiple times.

  blocksize SIZE_IN_KIBIBYTES
	Defines the basic block size in kibibytes for the parity.
	One kibibyte is 1024 bytes.

	The default blocksize is 256, which should work for most cases.

	WARNING! This option is for experts only, and it is highly
	recommended not to change this value. To change this value in the
	future, you will need to recreate the entire parity!

	A reason to use a different blocksize is if you have many small
	files, on the order of millions.

	For each file, even if only a few bytes, an entire block of parity is allocated,
	and with many files, this may result in significant unused parity space.
	When you completely fill the parity disk, you are not
	allowed to add more files to the data disks.
	However, the wasted parity does not accumulate across data disks. Wasted space
	resulting from a high number of files on a data disk limits only
	the amount of data on that data disk, not others.

	As an approximation, you can assume that half of the block size is
	wasted for each file. For example, with 100,000 files and a 256 KiB
	block size, you will waste 12.8 GB of parity, which may result
	in 12.8 GB less space available on the data disk.

	You can check the amount of wasted space on each disk using "status".
	This is the amount of space you must leave free on the data
	disks or use for files not included in the array.
	If this value is negative, it means you are close to filling
	the parity, and it represents the space you can still waste.

	To avoid this issue, you can use a larger partition for parity.
	For example, if the parity partition is 12.8 GB larger than the data disks,
	you have enough extra space to handle up to 100,000
	files on each data disk without any wasted space.

	A trick to get a larger parity partition in Linux is to format it
	with the command:

		:mkfs.ext4 -m 0 -T largefile4 DEVICE

	This results in about 1.5% extra space, approximately 60 GB for
	a 4 TB disk, which allows about 460,000 files on each data disk without
	any wasted space.

  hashsize SIZE_IN_BYTES
	Defines the hash size in bytes for the saved blocks.

	The default hashsize is 16 bytes (128 bits), which should work
	for most cases.

	WARNING! This option is for experts only, and it is highly
	recommended not to change this value. To change this value in the
	future, you will need to recreate the entire parity!

	A reason to use a different hashsize is if your system has
	limited memory. As a rule of thumb, SnapRAID typically requires
	1 GiB of RAM for each 16 TB of data in the array.

	Specifically, to store the hashes of the data, SnapRAID requires
	approximately TS*(1+HS)/BS bytes of RAM,
	where TS is the total size in bytes of your disk array, BS is the
	block size in bytes, and HS is the hash size in bytes.

	For example, with 8 disks of 4 TB, a block size of 256 KiB
	(1 KiB = 1024 bytes), and a hash size of 16, you get:

	:RAM = (8 * 4 * 10^12) * (1+16) / (256 * 2^10) = 1.93 GiB

	Switching to a hash size of 8, you get:

	:RAM = (8 * 4 * 10^12) * (1+8) / (256 * 2^10) = 1.02 GiB

	Switching to a block size of 512, you get:

	:RAM = (8 * 4 * 10^12) * (1+16) / (512 * 2^10) = 0.96 GiB

	Switching to both a hash size of 8 and a block size of 512, you get:

	:RAM = (8 * 4 * 10^12) * (1+8) / (512 * 2^10) = 0.51 GiB

  autosave SIZE_IN_GIGABYTES
	Automatically saves the state when syncing or scrubbing after the
	specified amount of GB processed.
	This option is useful to avoid restarting long "sync"
	commands from scratch if interrupted by a machine crash or any other event.

  temp_limit TEMPERATURE_CELSIUS
	Sets the maximum allowed disk temperature in Celsius. When specified,
	SnapRAID periodically checks the temperature of all disks using the
	smartctl tool. The current disk temperatures are displayed while
	SnapRAID is operating. If any disk exceeds this limit, all operations
	stop, and the disks are spun down (put into standby) for the duration
	defined by the "temp_sleep" option. After the sleep period, operations
	resume, potentially pausing again if the temperature limit is reached
	once more.

	During operation, SnapRAID also analyzes the heating curve of each
	disk and estimates the long-term steady temperature they are expected
	to reach if activity continues. This predicted steady temperature is
	shown in parentheses next to the current value and helps assess
	whether the system's cooling is adequate. This estimated temperature
	is for informational purposes only and has no effect on the behavior
	of SnapRAID. The program's actions are based solely on the actual
	measured disk temperatures.

	To perform this analysis, SnapRAID needs a reference for the ambient
	system temperature. It first attempts to read it from available
	hardware sensors. If no system sensor can be accessed, it uses the
	lowest disk temperature measured at the start of the run as a fallback
	reference.

	Normally, SnapRAID shows only the temperature of the hottest disk.
	To display the temperature of all disks, use the -A or --stats option.

  temp_sleep TIME_IN_MINUTES
	Sets the standby time, in minutes, when the temperature limit is
	reached. During this period, the disks remain spun down. The default
	is 5 minutes.

  pool DIR
	Defines the pooling directory where the virtual view of the disk
	array is created using the "pool" command.

	The directory must already exist.

  share UNC_DIR
	Defines the Windows UNC path required to access the disks remotely.

	If this option is specified, the symbolic links created in the pool
	directory use this UNC path to access the disks.
	Without this option, the symbolic links generated use only local paths,
	which does not allow sharing the pool directory over the network.

	The symbolic links are formed using the specified UNC path, adding the
	disk name as specified in the "data" option, and finally adding the
	file directory and name.

	This option is required only for Windows.

  smartctl DISK/PARITY OPTIONS...
	Defines custom smartctl options to obtain the SMART attributes for
	each disk. This may be required for RAID controllers and some USB
	disks that cannot be auto-detected. The %s placeholder is replaced by
	the device name, but it is optional for fixed devices like RAID controllers.

	DISK is the same disk name specified in the "data" option.
	PARITY is one of the parity names: "parity", "2-parity", "3-parity",
	"4-parity", "5-parity", "6-parity", or "z-parity".

	In the specified OPTIONS, the "%s" string is replaced by the
	device name. For RAID controllers, the device is
	likely fixed, and you may not need to use "%s".

	Refer to the smartmontools documentation for possible options:

		:https://www.smartmontools.org/wiki/Supported_RAID-Controllers
		:https://www.smartmontools.org/wiki/Supported_USB-Devices

	For example:

		:smartctl parity -d sat %s

  smartignore DISK/PARITY ATTR [ATTR...]
	Ignores the specified SMART attribute when computing the probability
	of disk failure. This option is useful if a disk reports unusual or
	misleading values for a particular attribute.

	DISK is the same disk name specified in the "data" option.
	PARITY is one of the parity names: "parity", "2-parity", "3-parity",
	"4-parity", "5-parity", "6-parity", or "z-parity".
	The special value * can be used to ignore the attribute on all disks.

	For example, to ignore the "Current Pending Sector Count" attribute on
	all disks:

		:smartignore * 197

	To ignore it only on the first parity disk:

		:smartignore parity 197

  Examples
	An example of a typical configuration for Unix is:

		:parity /mnt/diskp/snapraid.parity
		:content /mnt/diskp/snapraid.content
		:content /var/snapraid/snapraid.content
		:data d1 /mnt/disk1/
		:data d2 /mnt/disk2/
		:data d3 /mnt/disk3/
		:exclude /lost+found/
		:exclude /tmp/
		:smartctl d1 -d sat %s
		:smartctl d2 -d usbjmicron %s
		:smartctl parity -d areca,1/1 /dev/sg0
		:smartctl 2-parity -d areca,2/1 /dev/sg0

	An example of a typical configuration for Windows is:

		:parity E:\snapraid.parity
		:content E:\snapraid.content
		:content C:\snapraid\snapraid.content
		:data d1 G:\array\
		:data d2 H:\array\
		:data d3 I:\array\
		:exclude Thumbs.db
		:exclude \$RECYCLE.BIN
		:exclude \System Volume Information
		:smartctl d1 -d sat %s
		:smartctl d2 -d usbjmicron %s
		:smartctl parity -d areca,1/1 /dev/arcmsr0
		:smartctl 2-parity -d areca,2/1 /dev/arcmsr0

Pattern
	Patterns are used to select a subset of files to exclude or include in
	the process.

	There are four different types of patterns:

	=FILE
		Selects any file named FILE. You can use any globbing
		characters like * and ?, and character classes like [a-z].
		This pattern applies only to files, not directories.

	=DIR/
		Selects any directory named DIR and everything inside.
		You can use any globbing characters like * and ?.
		This pattern applies only to directories, not files.

	=/PATH/FILE
		Selects the exact specified file path. You can use any
		globbing characters like * and ?, but they never match a
		directory slash.
		This pattern applies only to files, not directories.

	=/PATH/DIR/
		Selects the exact specified directory path and everything
		inside. You can use any globbing characters like * and ?, but
		they never match a directory slash.
		This pattern applies only to directories, not files.

	When you specify an absolute path starting with /, it is applied at
	the array root directory, not the local file system root directory.

	In Windows, you can use the backslash \ instead of the forward slash /.
	Windows system directories, junctions, mount points, and other Windows
	special directories are treated as files, meaning that to exclude
	them, you must use a file rule, not a directory one.

	If the file name contains a '*', '?', '[',
	or ']' character, you must escape it to avoid having it interpreted as a
	globbing character. In Unix, the escape character is '\'; in Windows, it is '^'.
	When the pattern is on the command line, you must double the escape
	character to avoid having it interpreted by the command shell.

	In the configuration file, you can use different strategies to filter
	the files to process.
	The simplest approach is to use only "exclude" rules to remove all the
	files and directories you do not want to process. For example:

		:# Excludes any file named "*.unrecoverable"
		:exclude *.unrecoverable
		:# Excludes the root directory "/lost+found"
		:exclude /lost+found/
		:# Excludes any subdirectory named "tmp"
		:exclude tmp/

	The opposite approach is to define only the files you want to process, using
	only "include" rules. For example:

		:# Includes only some directories
		:include /movies/
		:include /musics/
		:include /pictures/

	The final approach is to mix "exclude" and "include" rules. In this case,
	the order of rules is important. Earlier rules take
	precedence over later ones.
	To simplify, you can list all the "exclude" rules first and then
	all the "include" rules. For example:

		:# Excludes any file named "*.unrecoverable"
		:exclude *.unrecoverable
		:# Excludes any subdirectory named "tmp"
		:exclude tmp/
		:# Includes only some directories
		:include /movies/
		:include /musics/
		:include /pictures/

	On the command line, using the -f option, you can only use "include"
	patterns. For example:

		:# Checks only the .mp3 files.
		:# In Unix, use quotes to avoid globbing expansion by the shell.
		:snapraid -f "*.mp3" check

	In Unix, when using globbing characters on the command line, you must
	quote them to prevent the shell from expanding them.

Content
	SnapRAID stores the list and checksums of your files in the content file.

	It is a binary file that lists all the files present in your disk array,
	along with all the checksums to verify their integrity.

	This file is read and written by the "sync" and "scrub" commands and
	read by the "fix", "check", and "status" commands.

Parity
	SnapRAID stores the parity information of your array in the parity
	files.

	These are binary files containing the computed parity of all the
	blocks defined in the "content" file.

	These files are read and written by the "sync" and "fix" commands and
	only read by the "scrub" and "check" commands.

Encoding
	SnapRAID in Unix ignores any encoding. It reads and stores the
	file names with the same encoding used by the file system.

	In Windows, all names read from the file system are converted and
	processed in UTF-8 format.

	To have file names printed correctly, you must set the Windows
	console to UTF-8 mode with the command "chcp 65001" and use
	a TrueType font like "Lucida Console" as the console font.
	This affects only the printed file names; if you
	redirect the console output to a file, the resulting file is always
	in UTF-8 format.

Copyright
	This file is Copyright (C) 2025 Andrea Mazzoleni

See Also
	rsync(1)
