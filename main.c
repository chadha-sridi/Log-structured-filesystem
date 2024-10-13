#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ADDR_CHECKPOINT_BLOCK 0
#define BLOCK_SIZE 4096
#define NUM_IMAP_PTRS_IN_CR  256
#define NUM_INODES_PER_IMAP_CHUNK 16
#define NUM_INODE_PTRS 14
#define NUM_DIR_ENTRIES 128
#define MAX_INODES 4096
#define ROOT_INODE 0
#define MAX_DISK_SIZE 1000 //max number of blocks logged in the disk
#define REG 0 // Regular file
#define DIR 1

typedef enum {
    CHECKPOINT_BLOCK,
    DIRECTORY_BLOCK,
    DATA_BLOCK,
    INODE_BLOCK,
    IMAP_CHUNK_BLOCK
} BlockType ;

typedef struct {
    int entries[NUM_IMAP_PTRS_IN_CR];
    int end_of_log_pointer;
} Checkpoint;

typedef struct {
    char name[28];
    int inode_number;
} DirectoryEntry;

typedef struct {
    DirectoryEntry entries[NUM_DIR_ENTRIES];
} DirectoryBlock;

typedef struct {
    int file_size;
    int file_type; //reg 0 or dir 1
    int pointers[NUM_INODE_PTRS];
} Inode;

typedef struct {
    int entries[NUM_INODES_PER_IMAP_CHUNK];
} ImapChunk;

typedef struct {
    char data[BLOCK_SIZE];
} DataBlock;

typedef struct {
    BlockType block_type;
    union {
        Checkpoint checkpoint_block;
        DirectoryBlock directory_block;
        DataBlock data_block;
        Inode inode_block;
        ImapChunk imap_chunk_block;
    } block;
} Block;                    // I introduced this structure because in C array elements have to be the same type

///end of structure definitions///



int imap[MAX_INODES];       //in-memory imap (indexes are inumbers and values are disk-addresses of inodes)
Block disk[MAX_DISK_SIZE];
int disk_len = 0;           //Global variable to keep track of disk length
int root_inode_address;
Checkpoint CR;
FILE * disk_file;
Checkpoint init_CR(){
    for (int i = 0; i < NUM_IMAP_PTRS_IN_CR; i++){
        CR.entries[i] = -1 ;
    }
    CR.end_of_log_pointer = 1 ; // CR is logged at address 0 so the eol is 1 currently
    return CR;
}


void init_imap(){
    for (int i = 0; i< MAX_INODES; i++){
        imap[i]= -1;
    }
    imap[ROOT_INODE] = root_inode_address; //which is 2 bc CR is at 0 and / is at 1
}


Inode make_inode(int ftype, int size) {
    Inode inode;
    inode.file_size = size;
    inode.file_type = ftype;
    for (int i = 0; i < NUM_INODE_PTRS; i++) {
        inode.pointers[i] = -1;
    }
    return inode;
}

DirectoryBlock make_directory_block(int parent_inum, int current_inum) {
    DirectoryBlock dir_block;
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        strcpy(dir_block.entries[i].name, "-");
        dir_block.entries[i].inode_number = -1;
    }
    dir_block.entries[0].inode_number = current_inum;
    strcpy(dir_block.entries[0].name, ".");
    dir_block.entries[1].inode_number = parent_inum;
    strcpy(dir_block.entries[1].name, "..");
    return dir_block;
}

DataBlock make_empty_data_block(){
    DataBlock data_block;
    memset(data_block.data, 0, BLOCK_SIZE); // Initialize data array to 0
    return data_block;
}
/// we need another function: fill data block invoked by write and copies 4096 bytes from the buffer

int inum_to_chunk(int inum){
    return (int)(inum/NUM_INODES_PER_IMAP_CHUNK); //which chunk (cnum) is "responsible" for that inode
}

ImapChunk make_imap_chunk(int chunk_num) {
    ImapChunk imap_chunk;
    int start = chunk_num * NUM_INODES_PER_IMAP_CHUNK;
    for (int i = start; i < start + NUM_INODES_PER_IMAP_CHUNK; i++) {
        imap_chunk.entries[i-start] = imap[i]; //takes the information from the in_memory imap
    }
    return imap_chunk;
}

 Block package_inode(Inode inode){
     Block block;
     block.block_type = INODE_BLOCK;
     block.block.inode_block = inode;
     return block;
 }

 Block package_directory_block(DirectoryBlock directory){
    Block block;
    block.block_type = DIRECTORY_BLOCK;
    block.block.directory_block = directory;
    return block;
}

Block package_data_block(DataBlock data){
    Block block;
    block.block_type = DATA_BLOCK;
    block.block.data_block = data;
    return block;
}

Block package_imap_chunk(ImapChunk imap_chunk){
    Block block;
    block.block_type = IMAP_CHUNK_BLOCK;
    block.block.imap_chunk_block = imap_chunk;
    return block;
}

Block package_checkpoint(Checkpoint checkpoint){
    Block block;
    block.block_type = CHECKPOINT_BLOCK;
    block.block.checkpoint_block = checkpoint;
    return block;
}

int log_block(Block block) {
    if (disk_len < MAX_DISK_SIZE) {
        int address = disk_len;
        disk[address] = block;
        disk_len++;
        CR.end_of_log_pointer = disk_len ;

        return address;
    } else {
        // Handle error when disk is full
        fprintf(stderr, "Error: Disk is full. Cannot log block.\n");
        exit(EXIT_FAILURE); // You can choose how to handle the error (exit, return an error code, etc.)
    }
}


void init_disk() {
    // Step 1: Log the checkpoint region
    CR = init_CR(); // initializing the Checkpoint Region
    log_block(package_checkpoint(CR));

    // Step 2: Create the root directory
    DirectoryBlock root_dir = make_directory_block(ROOT_INODE, ROOT_INODE);
    log_block(package_directory_block(root_dir));

    // Step 3: Create the root inode
    Inode root_inode = make_inode(1, 0);  // Assuming root is a directory with size 0
    root_inode.pointers[0] = 1;            // Root Inode points to the addr of the Root dir
    root_inode_address = log_block(package_inode(root_inode));

    // Step 4: Initialize the in-memory imap
    init_imap();

    // Step 5: Create the first imap chunk and log it
    ImapChunk first_imap_chunk  = make_imap_chunk(0); // 0 is cnum
    first_imap_chunk.entries[0] = root_inode_address;
    int first_imap_chunk_addr = log_block(package_imap_chunk(first_imap_chunk));
    CR.entries[0] = first_imap_chunk_addr ;
}


//****************************************//

// Function to print a Block
void print_block(Block block) {
    switch (block.block_type) {
        case CHECKPOINT_BLOCK:
            printf("Block Type: CHECKPOINT_BLOCK\n");
            printf("End of Log Pointer: %d\n", block.block.checkpoint_block.end_of_log_pointer);
            for (int i = 0; i < NUM_IMAP_PTRS_IN_CR; i++) {
                printf("%d", block.block.checkpoint_block.entries[i]);
                // Print a comma between entries, except after the last one
                    printf(", ");
                }
        case DIRECTORY_BLOCK:
            printf("Block Type: DIRECTORY_BLOCK\n");
            // Print directory entries
            for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
                if (block.block.directory_block.entries[i].inode_number != -1) {
                    printf("Entry %d: Name: %s, Inode Number: %d\n", i, block.block.directory_block.entries[i].name, block.block.directory_block.entries[i].inode_number);
                }
            }
            break;
        case DATA_BLOCK:
            printf("Block Type: DATA_BLOCK\n");
            // Print data or some representation
            printf("Data: %s\n", block.block.data_block.data);
            break;
        case INODE_BLOCK:
            printf("Block Type: INODE_BLOCK\n");
            printf("File Size: %d, File Type: %d\n", block.block.inode_block.file_size, block.block.inode_block.file_type);
            break;
        case IMAP_CHUNK_BLOCK:
            printf("Block Type: IMAP_CHUNK_BLOCK\n");
            for (int i = 0; i < NUM_INODES_PER_IMAP_CHUNK; i++) {
                if (block.block.imap_chunk_block.entries[i] != -1) {
                    printf("Inode %d Address: %d\n", i, block.block.imap_chunk_block.entries[i]);
                }
            }
            break;
        default:
            printf("Unknown Block Type\n");
            break;
    }
}

// Function to print the entire disk array
void print_disk() {
    for (int i = 0; i < disk_len; i++) {
        printf("Block %d:\n", i);
        print_block(disk[i]);
        printf("\n");
    }
}


///****************************************************************//
///FS operations

Inode get_inode_from_inumber(int inum) {
    // Check if the inode number is valid
    if (inum < 0 || inum >= MAX_INODES) {
        printf("Error: Invalid inode number.\n");
        exit(1);  // Handle the error as needed
    }

    // Get the address of the inode from the imap
    int inode_address = imap[inum] ; //* size(Block);
    if (inode_address == -1) {
        printf("Error: Inode not found.\n");
        exit(1);
    }

    // Read the inode from disk at the given address
    ///Inode inode; uncomment
    Inode inode = disk[inode_address].block.inode_block; ///Delete THIS later

    ///fseek(disk_file, inode_address, SEEK_SET);
    ///fread(&inode, sizeof(Inode), 1, disk_file);

    return inode;  // Return the retrieved inode
}
/**LOOKUP**/
int lookup(int pinum, char* name){
	//searches for a specific file of dir name within parent dir inode
 	Inode pInode = get_inode_from_inumber(pinum);
	assert(pInode.file_type == 1 && "Error: Parent inode is not a directory.");  //assert pInode is a dir inode
	// Iterate over pointers in the parent inode
	for (int i = 0; i < NUM_INODE_PTRS; i++) {
    	if (pInode.pointers[i] == -1) {
        	continue; // Skip empty pointers
    	}

    	// Access the directory block
    	Block directory_block = disk[pInode.pointers[i]];
    	if (directory_block.block_type != DIRECTORY_BLOCK) {
        	printf("Error: Block is not a directory block.\n");
        	return -1;
    	}

     	// Iterate over directory entries
    	for (int j = 0; j < NUM_DIR_ENTRIES; j++) {
        	DirectoryEntry entry = directory_block.block.directory_block.entries[j];
        	if (entry.inode_number != -1 && strcmp(entry.name, name) == 0) {
            	// Return inode number if name matches
            	printf("File Found.\n");
            	return entry.inode_number;
        	}
    	}
	}
	// Return -1 if not found
	//printf("File or Directory not found.\n");
	return -1;
}

/**Create**/

int create(int pinum, int type, char *name) {
    // 1: Lookup in parent directory to see if the file already exists
    int existing_inum = lookup(pinum, name);
    if (existing_inum != -1) {
        printf("Error: File or directory already exists.\n");
        return -1;
    }

    // 2: Get parent inode
    Inode pInode = get_inode_from_inumber(pinum);
    if (pInode.file_type != 1) {
        printf("Error: Parent inode is not a directory.\n");
        return -1;
    }

    // 3: Find a free directory entry in the parent directory
    int free_entry_index = -1;
    DirectoryBlock parent_dir_block;
    for (int i = 0; i < NUM_INODE_PTRS; i++) {
        if (pInode.pointers[i] != -1) {
            parent_dir_block = disk[pInode.pointers[i]].block.directory_block;
            for (int j = 0; j < NUM_DIR_ENTRIES; j++) {
                if (parent_dir_block.entries[j].inode_number == -1) {
                    free_entry_index = j;
                    printf("free_entry_index found %d at: \n", free_entry_index);
                    break;
                }
            }
            if (free_entry_index != -1) {
                break;
            }
        }
    }

    // Error: No space in the directory
    if (free_entry_index == -1) {
        printf("Error: No space in parent directory.\n");
        return -1;
    }

    // 4: Allocate a new inode
    int new_inum = -1;
    for (int i = 0; i < MAX_INODES; i++) {
        if (imap[i] == -1) {  // Find the next free inode
            new_inum = i;
            break;
        }
    }
    if (new_inum == -1) {
        printf("Error: No more available inodes.\n");
        return -1;
    }

    // 5: Create new inode
    Inode new_inode = make_inode(type, 0);  // New inode with the given type and size 0
    if (type == DIR) {
        // Initialize a new directory
        DirectoryBlock new_dir_block = make_directory_block(pinum, new_inum);
        int new_dir_block_addr = log_block(package_directory_block(new_dir_block));
        new_inode.pointers[0] = new_dir_block_addr;
    }
    else {
        // Handle regular file creation
        DataBlock new_data_block = make_empty_data_block(); // Create an empty data block for the file
        int new_data_block_addr = log_block(package_data_block(new_data_block)); // Log the data block
        new_inode.pointers[0] = new_data_block_addr; // Point to the data block
    }

    // 6: Log the new inode

    int new_inode_address = log_block(package_inode(new_inode));

    // 7: Update parent directory entry
    strcpy(parent_dir_block.entries[free_entry_index].name, name);
    parent_dir_block.entries[free_entry_index].inode_number = new_inum;

    // 8: Log the updated parent directory block
    int parent_dir_block_addr = log_block(package_directory_block(parent_dir_block));

    // 9: new parent inode's pointer
    int new_size = pInode.file_size + 1;
    Inode new_pInode = make_inode(1, new_size ) ;
    new_pInode.pointers[0] = disk_len - 1;  // Parent inode points to the last logged directory block

    // 10: Log the updated parent inode
    int updated_parent_inode_addr = log_block(package_inode(new_pInode));

    // 11: Update imap with new inode's address and updated parent inode's address
    imap[new_inum] = new_inode_address;
    imap[pinum] = updated_parent_inode_addr;

    // Step 12: Log the updated imap chunk
    int chunk_num = inum_to_chunk(new_inum);
    ImapChunk updated_imap_chunk = make_imap_chunk(chunk_num);
    int updated_imap_chunk_addr = log_block(package_imap_chunk(updated_imap_chunk));

    // Step 13: Update the checkpoint region
    CR.entries[chunk_num] = updated_imap_chunk_addr;
    disk[0] = package_checkpoint(CR);
    printf("File %s successfully created.\n",name);
    return 0;  // Success
}



int main() {

     disk_file = fopen("disk_log.bin", "wb");
    if (disk_file == NULL) {
        perror("fopen");
        exit(1);
    }

    init_disk();
    create(0, 0, "Test");
    lookup(0,"Test");
    for (int i = 0; i < 10; i++) {
        if (imap[i] != -1) {  // Only print valid entries
            printf("Inode %d -> Disk Address: %d\n", i, imap[i]);
        }}
    printf("**************************DISK**************************\n");
    print_disk();
    // Write the disk array to the file
    fwrite(disk, sizeof(Block), disk_len, disk_file);
    return 0;

}
