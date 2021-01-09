#ifndef __FAT12_H__
#define __FAT12_H__

#include <cinttypes>
#include <cstdlib>
#include <string>

using namespace std;

/* Allocation status */
#define ENTRY_DELETED	0xE5  
#define ENTRY_LAST		0x00

/* File attributes */
#define	ATTR_READONLY	0x01	/* read-only file */
#define	ATTR_HIDDEN	    0x02	/* hidden file */
#define	ATTR_SYSTEM	    0x04	/* system file */
#define	ATTR_VOLUME	    0x08	/* volume label */
#define	ATTR_DIRECTORY	0x10	/* directory */
#define	ATTR_ARCHIVE	0x20	/* archived */
#define	ATTR_LFN    	0x0F	/* LFN - long file name */

/* Clusters status */
#define EOC1            0xFF8
#define EOC2            0xFFF
#define CLUSTER_DAMAGED 0xFF7
#define CLUSTER_FREE    0x0000
#define CLUSTER_USED    0x0002
#define CLUSTER_USED2   0xFFF6

typedef uint64_t lba_t;
enum ENTRY_TYPE { FIL, DIR, IGNORE };
typedef enum ENTRY_TYPE ENTRY_TYPE;

typedef struct __attribute__((packed)) {
	uint8_t assembly_code_instructions[3]; //jump
	uint8_t OEM_name[8];
	uint16_t bytes_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t fat_count;
	uint16_t root_dir_capacity;
	uint16_t logical_sectors16; //Number of sectors in the file system
	uint8_t media_type;
	uint16_t sectors_per_fat;
	uint16_t sectors_per_track;
	uint16_t number_of_heads;
	uint32_t number_of_hidden_sectors; //number_of_sectors_before_the_start_partition
	uint32_t logical_sectors32; //Number of sectors in the file system
}boot_record;

typedef struct __attribute__((packed)) {
	boot_record bytes0_35;
	uint8_t drive_number;
	uint8_t not_used;
	uint8_t extended_boot_signature;
	uint32_t volume_serial_number;
	uint8_t volume_name[11];
	uint8_t file_system[8];
	uint8_t not_used2[448];
	uint16_t signature_value;
}boot_sector;

typedef struct __attribute__((packed)) {
	uint8_t filename[8]; //first byte is allocation status
	uint8_t extension[3];
	uint8_t attributes;
	uint8_t not_used;
	uint8_t creation_time_ms;
	uint16_t creation_time;
	uint16_t creation_date;
	uint16_t last_access_date;
	uint16_t first_cluster_1;
	uint16_t modify_time;
	uint16_t modify_date;
	uint16_t first_cluster_2;
	uint32_t size_of_file;
}dir_entry;

typedef struct {
	string path;
	dir_entry entry;
}dir_info;

typedef struct {
	uint64_t current_index;
	uint64_t size;
	uint64_t first_cluster;
}FILE_FAT;

/* commands functions */
int dir();
void cd(char*);
void cat(char*);
int get(char*);
int zip(char*, char*, char*);
void pwd();
void rootinfo();
void spaceinfo();
void fileinfo(char*);

/* FILE functions */
FILE_FAT* open_fat(char* name);
void close_fat(FILE_FAT* file);
size_t read_fat(FILE_FAT* file, void* buf);


size_t readblock(void* buffer, uint32_t first_block, size_t block_count);
lba_t cluster_start(uint64_t ncluster);

bool validate_fat();
dir_entry* find_entry(char* name, ENTRY_TYPE type);
void print_info(const dir_entry* entry);
size_t my_strlen(const char* text);
uint16_t next_cluster(uint16_t cluster);
void fatview();

#endif //__FAT12_H__