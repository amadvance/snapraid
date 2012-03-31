Name{number}
	snapraid - SnapRAID Backup For Disk Arrays

Synopsis
	:snapraid [-c, --conf CONFIG] [-f, --filter PATTERN] [-H, --filter-nohidden]
	:	[-N, --find-by-name]
	:	[-Z, --force-zero] [-E, --force-empty]
	:	[-s, --start BLKSTART] [-t, --count BLKCOUNT]
	:	[-v, --verbose]
	:	sync|diff|dup|check|fix

	:snapraid [-V, --version] [-h, --help]

Description
	SnapRAID is a backup program for a disk array.

	SnapRAID stores redundancy information in the disk array,
	and it allows recovering from up to two disk failures.

	SnapRAID is mainly targeted for a home media center, where you have
	a lot of big files that rarely change.

	Beside the ability to recover from disk failures, the other
	features of SnapRAID are:

	* You can start using SnapRAID with already filled disks.
	* The disks can have different sizes.
	* You can add disks at any time.
	* If you accidentally delete some files in a disk, you can
		recover them.
	* If more than two disks fail, you lose the data only on the
		failed disks. All the data in the other disks is safe.
	* It doesn't lock-in your data. You can stop using SnapRAID at any
		time without the need to reformat or move data.
	* All your data is hashed to ensure data integrity and to avoid
		silent corruption.

	The official site of SnapRAID is:

		:http://snapraid.sourceforge.net

Limitations
	SnapRAID is in between a RAID and a Backup program trying to get the best
	benefits of them. Although it also has some limitations that you should
	consider before using it.

	The main one is that if a disk fails, and you haven't recently synced,
	you may not able to do a complete recover.
	More specifically, you may be unable to recover up to the size of the
	amount of the changed or deleted files from the last sync operation.
	This happens even if the files changed or deleted are not in the
	failed disk.
	New added files don't prevent the recovering of the already existing
	files. You may only lose the just added files, if they are on the failed
	disk.

	This is the reason because SnapRAID is better suited for data that
	rarely change.

	Other limitations are:

	* You have different file-systems for each disk.
		Using a RAID you have only a big file-system.
	* It doesn't stripe data.
		With RAID you get a speed boost with striping.
	* It doesn't support real-time recovery.
		With RAID you do not have to stop working when a disk fails.
	* It's able to recover damages only from up to two disks.
		With a Backup you are able to recover from a complete
		failure of the whole disk array.
	* Only file data, time and symlinks are saved. Permissions,
		extended attributes and hard-links are not saved.

Getting Started
	To use SnapRAID you need to first select one disk of your disk array
	to dedicate at the "parity" information. With one disk for parity you
	will be able to recover from a single disk failure, like RAID5.

	If you want to recover from two disk failures, like RAID6, you must
	reserve another disk for the "q-parity" information.

	As parity disks, you have to pick the biggest disks in the array,
	as the redundancy information may grow in size as the biggest data
	disk in the array.

	These disks will be dedicated to store the "parity" and "q-parity"
	files. You should not store your data in them.

	The list of files is saved in the "content" files, usually
	stored in the data, parity or boot disks.
	These files contain the details of your backup, with all the
	checksums to verify its integrity.
	The "content" file is stored in multiple copies, and each one must
	be in a different disk, to ensure that in even in case of multiple
	disk failures at least one copy is available.

	For example, suppose that you are interested only at one parity level
	of protection, and that your disks are present in:

		:/mnt/diskpar <- selected disk for parity
		:/mnt/disk1 <- first disk to backup
		:/mnt/disk2  <- second disk to backup
		:/mnt/disk3 <- third disk to backup

	you have to create the configuration file /etc/snapraid.conf with
	the following options:

		:parity /mnt/diskpar/parity
		:content /var/snapraid/content
		:content /mnt/disk1/content
		:content /mnt/disk2/content
		:disk d1 /mnt/disk1/
		:disk d2 /mnt/disk2/
		:disk d3 /mnt/disk3/

	If you are in Windows, you should use drive letters and backslashes
	instead of slashes, and if you like, also file extensions.

		:parity E:\par\parity.par
		:content C:\snapraid\content.lst
		:content F:\array\content.lst
		:content G:\array\content.lst
		:disk d1 F:\array\
		:disk d2 G:\array\
		:disk d3 H:\array\

	At this point you are ready to start the "sync" command to build the
	redundancy information.

		:snapraid sync

	This process may take some hours the first time, depending on the size
	of the data already present in the disks. If the disks are empty
	the process is immediate.

	You can stop it at any time pressing Ctrl+C, and at the next run it
	will start where interrupted.

	When this command completes, your data is SAFE.

	At this point you can start using your array as you like, and periodically
	update the redundancy information running the "sync" command.

	To check the integrity of your data you can use the "check" command:

		:snapraid check

	If will read all your data, to check if it's correct.

	If an error is found, you can use the "fix" command to fix it.

		:snapraid fix

	Note that the fix command will revert your data at the state of the
	last "sync" command executed. It works like a snapshot was taken
	in "sync".

	In this regard snapraid is more like a backup program than a RAID
	system. For example, you can use it to recover from an accidentally
	deleted directory, simply running the fix command like.

		:snapraid fix -f DIR/

	Or to simply recover one file you can use:

		:snapraid fix -f FILE

Commands
	SnapRAID provides four simple commands that allow to:

	* Make a backup/snapshot -> "sync"
	* See the files changed from the previous sync -> "diff"
	* Check for integrity -> "check"
	* Restore the last backup/snapshot -> "fix".

	Take care that the commands have to be written in lower case.

  sync
	Updates the redundancy information. All the modified files
	in the disk array are read, and the redundancy data is
	recomputed.

	Files are identified by inode and checked by time and size,
	meaning that you can move them on the disk without triggering
	any redundancy recomputation.

	You can stop this process at any time pressing Ctrl+C,
	without losing the work already done.

	The "content", "parity" and "q-parity" files are modified if necessary.
	The files in the array are NOT modified.

  diff
	Lists all the files modified from the last "sync" command that
	have to recompute their redundancy data.

	Nothing is modified.

  dup
	Lists all the duplicate files. Two files are assumed equal if their hashes
	are matching. The effective data is not read.

	Nothing is modified.

  check
	Checks all the files and the redundancy data.
	All the files are hashed and compared with the snapshot saved
	in the previous "sync" command.

	Files are identified by path, and checked by content.
	Nothing is modified.

  fix
	Checks and fix all the files. It's like "check" but it
	also tries to fix problems reverting the state of the
	disk array to the previous "sync" command.

	After a successful "fix", you should also run a "sync"
	command to update the new state of the files.

	All the files that cannot be fixed are renamed adding
	the ".unrecoverable" extension.

	The "content" file is NOT modified.
	The "parity" and "q-parity" files are modified if necessary.
	The files in the array are modified if necessary.

Options
	SnapRAID provides the following options:

	-c, --conf CONFIG
		Selects the configuration file. If not specified it's assumed
		the file "/etc/snapraid.conf" in Unix, and "snapraid.conf" in
		the current directory in Windows.

	-f, --filter PATTERN
		Filters the files to operate on with the "check" and "fix"
		commands. This option is ignored with the "sync" command.
		See the PATTERN section for more details in the
		pattern specifications.
		This option can be used many times.
		In Unix, ensure to quote globbing chars if used.

	-H, --filter-nohidden
		Filters out hidden files and directory. In Unix hidden files are
		the ones starting with '.'. In Windows they are the ones with the
		hidden attribute.

	-N, --find-by-name
		When syncing finds the files by path instead than by inode.
		This option allows a fast sync command after having replaced
		one physical disk with another, copying manually the files.
		Without this option the "sync" command recognizes that
		the files were copied to a different disk, and it will resync
		them all. With this option, a file with the correct path,
		size and time is assumed identical at the previous one,
		and not resynched.
		This option has effect only on the "sync" and "diff" commands.

	-Z, --force-zero
		Forces the insecure operation of syncing a file with zero
		size that before was not.
		If SnapRAID detects a such condition, it stops proceeding
		unless you specify this option.
		This allows to easily detect when after a system crash,
		some accessed files were zeroed.
		This is a possible condition in Linux with the ext3/ext4
		filesystems.

	-E, --force-empty
		Forces the insecure operation of syncing a disk with all
		the original files missing.
		If SnapRAID detects that all the files originally present
		in the disk are missing or rewritten, it stops proceeding
		unless you specify this option.
		This allows to easily detect when a data file-system is not
		mounted.

	-s, --start BLKSTART
		Starts the processing from the specified
		block number. It could be useful to easy retry to check
		or fix some specific block, in case of a damaged disk.

	-t, --count BLKCOUNT
		Process only the specified number of blocks.
		It's present mainly for advanced manual recovering.

	-v, --verbose
		Prints more information in the processing.

	-h, --help
		Prints a short help screen.

	-V, --version
		Prints the program version.

Configuration
	SnapRAID requires a configuration file to know where your disk array
	is located, and where storing the redundancy information.

	This configuration file is located in /etc/snapraid.conf in Unix or
	in the execution directory in Windows.

	It should contain the following options (case sensitive):

  parity FILE
	Defines the file to use to store the parity information.
	The parity enables the protection from a single disk
	failure, like RAID5.
	
	It must be placed in a disk dedicated for this purpose with
	as much free space as the biggest disk in the array.
	Leaving the parity disk reserved for only this file ensures that
	it doesn't get fragmented, improving the performance.

	This option is mandatory and it can be used only one time.

  q-parity FILE
	Defines the file to use to store the q-parity information.
	If present, the q-parity enables the protection from two disk
	failures, like RAID6.

	It must be placed in a disk dedicated for this purpose with
	as much free space as the biggest disk in the array.
	Leaving the q-parity disk reserved for only this file ensures that
	it doesn't get fragmented, improving the performance.

	This option is optional and it can be used only one time.

  content FILE
	Defines the file to use to store the list and checksums of all the
	files present in your disk array.

	It can be placed in the disk used to store data, parity, or
	any other disk available.
	If you use a data disk, this file is automatically excluded
	from the "sync" process.

	This option is mandatory and it can be used more time to save
	more copies of the same files.

	You have to store at least one copy for each parity disk used
	plus one. Using some more don't hurt.

  disk NAME DIR
	Defines the name and the mount point of the disks of the array.
	NAME is used to identify the disk, and it must be unique.
	DIR is the mount point of the disk in the filesystem.

	You can change the mount point as you like, as long you
	keep the NAME fixed.

	You should use one option for each disk of the array.

  nohidden
	Excludes all the hidden files and directory, like the
	--filter-nohidden option.

  exclude/include PATTERN
	Defines the file or directory patterns to exclude and include
	in the sync process.
	All the patterns are processed in the specified order.

	If the first pattern that matches is an "exclude" one, the file
	is excluded. If it's an "include" one, the file is included.
	If no pattern matches, the file is excluded if the last pattern
	specified is an "include", or included if the last pattern
	specified is an "exclude".

	See the PATTERN section for more details in the pattern
	specifications.

	This option can be used many times.

  block_size SIZE_IN_KIBIBYTES
	Defines the basic block size in kibi bytes of the redundancy
	blocks. Where one kibi bytes is 1024 bytes.
	The default is 256 and it should work for most conditions.
	You could increase this value if you do not have enough RAM
	memory to run SnapRAID.

	As a rule of thumb, with 4 GiB or more memory use the default 256,
	with 2 GiB use 512, and with 1 GiB use 1024.

	In more details SnapRAID requires about TS*24/BS bytes
	of RAM memory to run. Where TS is the total size in bytes of
	your disk array, and BS is the block size in bytes.

	For example with 6 disk of 2 TiB and a block size of 256 KiB
	(1 KiB = 1024 Bytes) you have:

	:RAM = (6 * 2 * 2^40) * 24 / (256 * 2^10) = 1.1 GiB

	You could instead decrease this value if you have a lot of
	small files in the disk array. For each file, even if of few
	bytes, a whole block is always allocated, so you may have a lot
	of unused space.
	As approximation, you can assume that half of the block size is
	wasted for each file.

	For example, with 10000 files and a 256 KiB block size, you are
	going to waste 1.2 GiB.

    autosave SIZE_IN_GIBIBYTES
	Automatically save the state when synching after the specied amount
	of GiB processed.
	This option is useful to avoid to restart from scratch long 'sync'
	commands interrupted by a machine crash, or any other event that
	may interrupt SnapRAID.
	The SIZE argument is specified in gibibytes. Where one gibi bytes
	is 1073741824 bytes.

  Examples
	An example of a typical configuration for Unix is:

		:parity /mnt/diskpar/parity
		:content /mnt/diskpar/content
		:content /var/snapraid/content
		:disk d1 /mnt/disk1/
		:disk d2 /mnt/disk2/
		:disk d3 /mnt/disk3/
		:exclude *.bak
		:exclude /lost+found/
		:exclude /tmp/

	An example of a typical configuration for Windows is:

		:parity E:\par\parity
		:content E:\par\content
		:content C:\snapraid\content
		:disk d1 G:\array\
		:disk d2 H:\array\
		:disk d3 I:\array\
		:exclude *.bak
		:exclude Thumbs.db
		:exclude \$RECYCLE.BIN\
		:exclude \System Volume Information\

Pattern
	Patterns are used to select a subset of files to exclude or include in
	the process.

	There are four different types of patterns:

	=FILE
		Selects any file named as FILE. You can use any globbing
		character like * and ?.
		This pattern is applied only to files and not to directories.

	=DIR/
		Selects any directory named DIR. You can use any globbing
		character like * and ?.
		This pattern is applied only to directories and not to files.

	=/PATH/FILE
		Selects the exact specified file path. You can use any
		globbing character like * and ? but they never matches a
		directory slash.
		This pattern is applied only to files and not to directories.

	=/PATH/DIR/
		Selects the exact specified directory path. You can use any
		globbing character like * and ? but they never matches a
		directory slash.
		This pattern is applied only to directories and not to files.

	In Windows you can freely use the backslash \ instead of the forward slash /.

	In the configuration file, you can use different strategies to filter
	the files to process.
	The simplest one is to only use "exclude" rules to remove all the
	files and directories you do not want to process. For example:

		:# Excludes any file named "*.bak"
		:exclude *.bak
		:# Excludes the root directory "/lost+found"
		:exclude /lost+found/
		:# Excludes any sub-directory named "tmp"
		:exclude tmp/

	The opposite way is to define only the file you want to process, using
	only "include" rules. For example:

		:# Includes only some directories
		:include /movies/
		:include /musics/
		:include /pictures/

	The final way, is to mix "exclude" and "include" rules. In this case take
	care that the order of rules is important. Previous rules have the
	precedence over the later ones.
	To get things simpler you can first have all the "exclude" rules and then
	all the "include" ones. For example:

		:# Excludes any file named "*.bak"
		:exclude *.bak
		:# Excludes any sub-directory named "tmp"
		:exclude tmp/
		:# Includes only some directories
		:include /movies/
		:include /musics/
		:include /pictures/

	On the command line, using the -f option, you can only use "include"
	patterns. For example:

		:# Checks only the .mp3 files.
		:# Note the "" use to avoid globbing expansion by the shell in Unix.
		:snapraid -f "*.mp3" check

	In Unix, when using globbing chars in the command line, you have to quote them.
	Otherwise the shell will try to expand them.

Recovering
	The worst happened, and you lost a disk!

	DO NOT PANIC! You will be able to recover it!

	The first thing you have to do is to avoid futher changes at you disk array.
	Disable any remote connection to it, any scheduled process, including any
	scheduled SnapRAID nightly sync.

	Then proceed with the following steps.

    STEP 1 -> Reconfigure
	You need some space to recover, even better if you already have an additional
	disk, but in case, also an external USB or remote one is enough.
    
	Change the SnapRAID configuration and change the "disk" directory
	of the failed disk to point to the new empty space.

    STEP 2 -> Fix
	Run the fix command, storing the log in an external file with:

		:snapraid fix 2>fix.log

	This command will take a long time.

	Take care that you need also few gigabytes free to store the fix.log file, so run it
	from a disk with some free space.

	Now you have recovered all the recoverable. If some file is partially or totally
	unrecoverable, it will be renamed adding the ".unrecoverable" extension.

	You can get a detailed list of all the unrecoverable blocks in the fix.log file
	checking all the lines starting with "unrecoverable:"

	If you are not satified of the recovering, you can retry it as many time you wish.
	For example, if you have moved away some files from other disks after the last "sync",
	you can retry to put them inplace, and retry the "fix".

	If you are satisfied of the recovering, you can now proceed further,
	but take care that after synching you will no more able to retry the
	"fix" command!

    STEP 3 -> Check
	As paranoid check, you can now run a whole "check" command to ensure that
	everything is OK.

		:snapraid check

	This command will take a long time.

    STEP 4 -> Sync
	Run the "sync" command to resyncronize the array with the new disk.

	To avoid a long time sync you can use the "--find-by-name" option to
	force SnapRAID to ignore the fact that all the recovered files are now in
	a different physical disk, but they are not changed.

		:snapraid --find-by-name sync

	If everything was recovered, this command is immediate.

Content
	SnapRAID stores the list and checksums of your files in the content file.

	It's a text file, listing all the files present in your disk array,
	with all the checksums to verify their integrity.

	You do not need to understand its format, but it's described here
	for documentation.

	This file is read and written by the "sync" command, and only read by
	"fix" and "check".

  blk_size SIZE
	Defines the size of the block in bytes. It must match the size
	defined in the configuration file.

  checksum CHECKSUM
	Defines the checksum kind used. It can be "md5" or "murmur3".

  map NAME INDEX
	Defines the position INDEX of the disk NAME in the parity computation.

  sign SIGN
	Signature checksum of the content file to ensure that it doesn't get
	corrupted. If you want to modify the content file manually, you have
	to remove this line to avoid this check.

  file DISK SIZE TIME INODE PATH
	Defines a file in the specified DISK.

	The INODE number is used to identify the file in the "sync"
	command, allowing to rename or move the file in disk without
	the need to recompute the parity for it.

	The SIZE and TIME information are used to identify if the file
	changed from the last "sync" command, and if there is the need
	to recompute the parity.

	The PATH information is used in the "check" and "fix" commands
	to identify the file.

  blk BLOCK HASH
	Defines an ordered parity block, part of the last defined file.

	BLOCK is the block position in the "parity" file.
	0 for the first block, 1 for the second one and so on.

	HASH is the hash of the block. In the last block of the file,
	the HASH is the hash of only the used part of the block.

  inv BLOCK HASH
	Like "blk", but inform that the parity of this block is invalid.

	This field is used only when you interrupt manually the "sync"
	command.

  new BLOCK
	Like "blk", but for new allocated blocks for which the hash is not
	yet computed, and the stored parity doesn't take into account this
	new block.

	This field is used only when you interrupt manually the "sync"
	command.

  chg BLOCK
	Like "blk", but for reallocated blocks for which the hash is not
	yet computed, and the parity is computed using the previous value
	of the block.

	This field is used only when you interrupt manually the "sync"
	command.

  hole DISK
	Defines the list of blocks that are deleted from a disk.

	This field is used only when you interrupt manually the "sync"
	command.

  off BLOCK
	Defines a block deleted from a disk, part of the last defined hole,
	for which the parity is computed using the previous value.

	This field is used only when you interrupt manually the "sync"
	command.

Parity
	SnapRAID stores the redundancy information of your array in the parity
	and q-parity files.

	They are binary files, containing the computed redundancy of all the
	blocks defined in the "content" file.

	You do not need to understand its format, but it's described here
	for documentation.

	These files are read and written by the "sync" and "fix" commands, and
	only read by "check".

	For all the blocks at a given position, the parity and the q-parity
	are computed as specified in:

		:http://kernel.org/pub/linux/kernel/people/hpa/raid6.pdf

	When a file block is shorter than the default block size, for example
	because it's the last block of a file, it's assumed as filled with 0
	at the end.

Encoding
	SnapRAID in Unix ignores any encoding. It simply reads and stores the
	file names with the same encoding used by the filesystem.

	In Windows all the names read from the filesystem are converted and
	processed in the UTF-8 format.

	To have the file names printed correctly you have to set the Windows
	console in the UTF-8 mode, with the command "chcp 65001", and use
	a TrueType font like "Lucida Console" as console font.
	Note that it has effect only on the printed file names, if you
	redirect the console output to a file, the resulting file is always
	in the UTF-8 format.

Copyright
	This file is Copyright (C) 2011 Andrea Mazzoleni

See Also
	rsync(1)

