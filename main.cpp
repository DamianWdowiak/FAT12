#include <iostream>
#include <cstdio>
#include <cstring>
#include <vector>
#include "fat12.h"

using namespace std;

extern lba_t volume_start;
extern lba_t fat1_start;
extern lba_t fat2_start;
extern lba_t root_start;
extern lba_t sectors_per_root;
extern lba_t data_start;
extern lba_t available_clusters;
extern boot_sector boot;
extern char* filename;
extern vector<dir_info>directory;

#define COMMAND_SIZE 500

int main(int argc, char** argv)
{
	if (argc < 2) {
		printf("Too few arguments!\n");
		return 2;
	}
	boot.bytes0_35.bytes_per_sector = 0;
	filename = *(argv + 1);

	if (!readblock(&boot, 0, 1)) {
		printf("Can't open your disk's file!\n");
		return 3;
	}
	if (!validate_fat()) {
		printf("Not a valid FAT12 filesystem!\n");
		return 3;
	}
	volume_start = boot.bytes0_35.number_of_hidden_sectors;
	fat1_start = volume_start + boot.bytes0_35.reserved_sectors;
	fat2_start = fat1_start + boot.bytes0_35.sectors_per_fat;
	root_start = fat1_start + 2 * boot.bytes0_35.sectors_per_fat;
	sectors_per_root = boot.bytes0_35.root_dir_capacity * sizeof(dir_entry) / boot.bytes0_35.bytes_per_sector;
	data_start = root_start + sectors_per_root;
	available_clusters = (boot.bytes0_35.logical_sectors16 - boot.bytes0_35.reserved_sectors - 2 * boot.bytes0_35.sectors_per_fat - sectors_per_root) / boot.bytes0_35.sectors_per_cluster;

	if (argc == 3 && (!strcmp(*(argv + 2), "-d") || !strcmp(*(argv + 2), "-debug"))) {
		printf("Assembly_code_instructions: %x ", boot.bytes0_35.assembly_code_instructions[0]);
		printf("%x ", boot.bytes0_35.assembly_code_instructions[1]);
		printf("%x \n", boot.bytes0_35.assembly_code_instructions[2]);
		printf("OEM_NAME: ");
		for (int i = 0;boot.bytes0_35.OEM_name[i] != '\0' && boot.bytes0_35.OEM_name[i] != ' ';i++)
			printf("%c", boot.bytes0_35.OEM_name[i]);

		printf("\nBytes per sector: %u\n", boot.bytes0_35.bytes_per_sector);
		printf("\nSectors per cluster: %u", boot.bytes0_35.sectors_per_cluster);
		printf("\nSize of reserved area, in sectors: %u\n", boot.bytes0_35.reserved_sectors);
		printf("Number of FATs (usually 2): %u\n", boot.bytes0_35.fat_count);
		printf("Maximum number of files in the root directory: %u\n", boot.bytes0_35.root_dir_capacity);
		printf("Number of sectors in the file system(16): %u\n", boot.bytes0_35.logical_sectors16);
		printf("Media type: %u\n", boot.bytes0_35.media_type);
		printf("Sectors per FAT: %u\n", boot.bytes0_35.sectors_per_fat);
		printf("Sectors per root: %lu\n", sectors_per_root);
		printf("Sectors per track in storage device: %u\n", boot.bytes0_35.sectors_per_track);
		printf("Number of heads in storage device: %u\n", boot.bytes0_35.number_of_heads);
		printf("Number of sectors before the start partition: %u\n", boot.bytes0_35.number_of_hidden_sectors);
		printf("Number of sectors in the file system(32): %u\n", boot.bytes0_35.logical_sectors32);
		printf("Number of available clusters: %lu\n", available_clusters);

		printf("File system table name: ");
		for (int i = 0;boot.file_system[i] != ' ' && boot.file_system[i] != '\0';++i)
			printf("%c", boot.file_system[i]);
		printf("\n\n");
	}
	char command[COMMAND_SIZE] = { 0 };
	char* token;
	do {
		fatview();
		if (!fgets(command, COMMAND_SIZE, stdin))
			return 2;
		if (*(command + strlen(command) - 1) != '\n')
			while (getchar() != '\n');
		token = strtok(command, " \n");
		if (token) {
			if (!strcmp(token, "cd")) {
				token = strtok(NULL, " \n");
				cd(token);
			}
			else if (!strcmp(token, "dir")) {
				dir();
			}
			else if (!strcmp(token, "pwd")) {
				pwd();
			}
			else if (!strcmp(token, "cat")) {
				token = strtok(NULL, " \n");
				cat(token);
			}
			else if (!strcmp(token, "get")) {
				token = strtok(NULL, " \n");
				int err = 0;
				if ((err = get(token)) == 0)
					cout << "get: success: File copied" << endl;
				else if (err == 2) {
					cout << "get: " << token << ": No such file" << endl;
				}
				else if (err == 3) {
					cout << "get: Allocation problem" << endl;
				}
				else if (err == 4) {
					cout << "get: Can't create output file" << endl;
				}
				else if (err == 7) {
					cout << "get: " << token << ": File exists on local disk" << endl;
				}
			}
			else if (!strcmp(token, "zip")) {
				token = strtok(NULL, " \n");
				string copy_token;
				if (token)
					copy_token = token;
				char* token2 = strtok(NULL, " \n");
				string copy_token2;
				if (token2)
					copy_token2 = token2;
				char* token3 = strtok(NULL, " \n");
				int err = 0;
				if ((err = zip(token, token2, token3)) == 0)
					cout << "zip: success: Content from <" << copy_token << "> and <" << copy_token2 << "> copied into local file <" << token3 << ">" << endl;
				else if (err == 2) {
					cout << "zip: " << copy_token << ": No such file" << endl;
				}
				else if (err == 5) {
					cout << "zip: " << copy_token2 << ": No such file" << endl;
				}
				else if (err == 3) {
					cout << "zip: Allocation problem" << endl;
				}
				else if (err == 4) {
					cout << "zip: " << token3 << ": Can't create output file" << endl;
				}
				else if (err == 1) {
					cout << "zip: Too few arguments!: zip <name1> <name2> <name3>" << endl;
				}
				else if (err == 7) {
					cout << "zip: " << token3 << ": File exists on local disk" << endl;
				}
			}
			else if (!strcmp(token, "rootinfo")) {
				rootinfo();
			}
			else if (!strcmp(token, "spaceinfo")) {
				spaceinfo();
			}
			else if (!strcmp(token, "fileinfo")) {
				token = strtok(NULL, " \n");
				fileinfo(token);
			}
			else if (!strcmp(token, "exit")) {
				break;
			}
		}
	} while (1);

	readblock(NULL, 0, 0); //fclose
	return 0;
}