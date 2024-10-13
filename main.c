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
            break;
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
    int inode_address = 2; //imap[inum] * size(Block); /// work here
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
            	return entry.inode_number;
        	}
    	}
	}
	// Return -1 if not found
	printf("File or Directory not found");
	return -1;
}



int main() {

     disk_file = fopen("disk_log.bin", "wb");
    if (disk_file == NULL) {
        perror("fopen");
        exit(1);
    }

    init_disk();
    //print_disk();
    printf("CR end_of_log_pointer: %d\n", CR.end_of_log_pointer);

    // Write the disk array to the file
    fwrite(disk, sizeof(Block), disk_len, disk_file);


    // Close the file
    // fclose(file);
    /*FILE * disk_file = fopen("disk_log.bin", "rb");  // 'rb' for reading binary

    if (file == NULL) {
        perror("fopen");
        exit(1);
    }

    // Allocate memory to read the disk array
    Block *read_disk = malloc(sizeof(Block) * disk_len);
    if (read_disk == NULL) {
        perror("malloc");
        fclose(file);
        exit(1);
    }

    fread(read_disk, sizeof(Block), disk_len, file);
    fclose(file);

    // Print the disk contents
    for (int i = 0; i < disk_len; i++) {
        print_block(read_disk[i]);
    }

    // Free allocated memory
    free(read_disk); */

    return 0;

}
