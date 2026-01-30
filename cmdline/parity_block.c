
#include "portable.h"

#include "elem.h"
#include "parity.h"
#include "state.h"
#include "stream.h"
#include "parity_block.h"

struct snapraid_parity_entry
{
	block_off_t max_parity_pos;
    struct snapraid_file* file;
	struct snapraid_disk* disk;	

    tommy_node node; /* for list */
};

int parity_entry_file_path_compare(const void* void_a, const void* void_b)
{
	const struct snapraid_parity_entry* entry_a = void_a;
	const struct snapraid_parity_entry* entry_b = void_b;

	int diskCmp = strcmp(entry_a->disk->dir, entry_b->disk->dir);
	if(diskCmp == 0)
		return 0;

	return strcmp(entry_a->file->sub, entry_b->file->sub);
}

void dump_entry(struct snapraid_parity_entry* entry)
{
	printf("%s|%"PRIu64"|%d|%s%s\n",
               entry->disk->name,			   
			   (uint64_t)entry->file->size,
			   entry->max_parity_pos,
			   entry->disk->dir, entry->file->sub);
}

void add_size_to_sum(void* arg, void* obj)
{
	struct snapraid_parity_entry* entry = obj;
	data_off_t* sizeSum = (data_off_t*)arg;
	*sizeSum += entry->file->size;
}

static void collect_parity_block_file(struct snapraid_disk* disk,
                                      struct snapraid_file* file,
									  tommy_list* fileList,
									  block_off_t minOccupiedBlockNumber)
{	
	block_off_t max_parity_pos = 0;
	for (block_off_t i = 0; i < file->blockmax; ++i)
	{
		block_off_t parity_pos = fs_file2par_find(disk, file, i);
		// Check if a valid position is found (assuming -1 or some invalid value if not mapped)
		if (parity_pos == (block_off_t)-1)
		{
			// ignore - what to do?
			fprintf(stderr, "Found invalid parity postion for file: %s%s\n", disk->dir, file->sub);
			continue;
		}

		if(parity_pos > max_parity_pos)
			max_parity_pos = parity_pos;	
	}
	
	if(max_parity_pos == 0 || max_parity_pos < minOccupiedBlockNumber)
		return; // entry not relevant

    // found a relevant block so add the corresponding file
	struct snapraid_parity_entry* entry = malloc_nofail(sizeof(struct snapraid_parity_entry));
	entry->file = file;
	entry->disk = disk;
	entry->max_parity_pos = max_parity_pos;
	tommy_list_insert_tail(fileList, &entry->node, entry);
}

void dump_parity_files_for_shrink(struct snapraid_state* state, unsigned int parityToShrinkInMegaBytes)
{	
    if (!state) {
        fprintf(stderr, "State pointer is NULL\n");
        return;
    }

	if(state->level == 0) {
		fprintf(stderr, "No parity found\n");
		return;
	}
	
	printf("\n");
	printf("Parity to shrink by: %d mb\n\n", parityToShrinkInMegaBytes);

	uint32_t block_size = state->block_size;
	printf("Block size: %d kb\n", block_size / 1024);

	// calculate required parity blocks to shrink
	data_off_t maxParitySizeInBytes = 0;
	for(unsigned int levelIndex=0; levelIndex < state->level; levelIndex++)
	{
		struct snapraid_parity* parity = &state->parity[levelIndex];		
		struct snapraid_parity_handle parity_handle;
		struct snapraid_parity_handle* parity_handle_ptr = &parity_handle;
		int res = parity_open(parity_handle_ptr, parity, levelIndex, state->file_mode, state->block_size, state->opt.parity_limit_size);		
		if(res != 0)
		{
			fprintf(stderr, "Can't read parity size\n");
			return;
		}

		data_off_t parity_size_out;
		parity_size(parity_handle_ptr, &parity_size_out);		
		parity_close(parity_handle_ptr);

		if(parity_size_out > maxParitySizeInBytes)
			maxParitySizeInBytes = parity_size_out;
	}
	
	if(maxParitySizeInBytes == 0) {
		fprintf(stderr, "No parity size found\n");
		return;
	}

	printf("Max parity size is: %"PRIu64" mb\n", maxParitySizeInBytes / 1024 / 1024);
	block_off_t maxOccupiedBlockNumber = maxParitySizeInBytes / block_size;
	printf("Current max parity block: %d\n", maxOccupiedBlockNumber);
	
	data_off_t parityToShrinkInBytes = (data_off_t)parityToShrinkInMegaBytes * 1024 * 1024;
	block_off_t requiredBlocksToShrink = parityToShrinkInBytes / block_size + 1;		
	block_off_t minOccupiedBlockNumber = maxOccupiedBlockNumber - requiredBlocksToShrink;
	printf("New max parity block should be: %d\n", minOccupiedBlockNumber);
	printf("Parity blocks to shrink: %d\n", requiredBlocksToShrink);

	// collect relevant parity blocks
	printf("\n");
	msg_progress("Collecting files with max parity block greater %d...\n", minOccupiedBlockNumber);	
	tommy_list relevantFiles;
	tommy_list_init(&relevantFiles);	 
	for (tommy_node* diskNode = tommy_list_head(&state->disklist); diskNode != 0; diskNode = diskNode->next) {
		struct snapraid_disk* disk = diskNode->data;

		for (tommy_node* fileNode = tommy_list_head(&disk->filelist); fileNode != 0; fileNode = fileNode->next)			
			collect_parity_block_file(disk, fileNode->data, &relevantFiles, minOccupiedBlockNumber);
	}

	// dump result to console
	printf("\n");
	if(tommy_list_count(&relevantFiles) == 0)
	{
		printf("No files found to shrink the required parity space.\n");
	}
	else
	{	
		// sort by path
		tommy_list_sort(&relevantFiles, parity_entry_file_path_compare);
		// sum total bytes to move
		data_off_t totalSizeToMoveBytes = 0;
		tommy_list_foreach_arg(&relevantFiles, (tommy_foreach_arg_func*)add_size_to_sum, &totalSizeToMoveBytes);
		uint32_t totalSizeToMoveInMegaBytes = totalSizeToMoveBytes / 1024 / 1024 + 1;

		printf("Result:\n");
		printf("Total data to temporarily move: %d mb\n", totalSizeToMoveInMegaBytes);
		printf("Files to be moved to shrink parity:\n\n");
		printf("DataDisk|Size|MaxParityBlock|Path\n");
		printf("----------------------------------------\n");
		tommy_list_foreach(&relevantFiles, (tommy_foreach_func*)dump_entry);
	}

	// cleanup
	tommy_list_foreach(&relevantFiles, free);
}