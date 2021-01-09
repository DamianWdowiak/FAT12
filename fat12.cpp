#include "fat12.h"
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <ctype.h>
#include <math.h>

using namespace std;

lba_t volume_start;
lba_t fat1_start;
lba_t fat2_start;
lba_t root_start;
lba_t sectors_per_root;
lba_t data_start;
lba_t available_clusters;
boot_sector boot;
FILE* disksfile;
char* filename;
vector<dir_info>directory;

size_t readblock(void* buffer, uint32_t first_block, size_t block_count) {
	if (!buffer) {
		fclose(disksfile);
		return 0;
	}

	if (!boot.bytes0_35.bytes_per_sector) {
		disksfile = fopen(filename, "rb");
		if (!disksfile)
			return 0;
	}

	fseek(disksfile, first_block * boot.bytes0_35.bytes_per_sector, SEEK_SET);
	if (!boot.bytes0_35.bytes_per_sector)
		return fread(buffer, sizeof(boot_sector), block_count, disksfile);
	return fread(buffer, boot.bytes0_35.bytes_per_sector, block_count, disksfile);
}

lba_t cluster_start(uint64_t ncluster) {
	return data_start + (ncluster - 2) * boot.bytes0_35.sectors_per_cluster;
}

int dir() {
	int dirs = 0;
	int files = 0;
	uint64_t size_of_files = 0;
	dir_entry* entries;
	if (directory.empty()) {
		/* root directory */
		entries = (dir_entry*)malloc(boot.bytes0_35.root_dir_capacity * sizeof(dir_entry));
		if (!entries) {
			return 2;
		}
		if (readblock(entries, root_start, sectors_per_root) != sectors_per_root) {
			free(entries);
			return 1;
		}
		for (unsigned i = 0; (entries + i)->filename[0] != ENTRY_LAST; ++i) {
			if ((entries + i)->filename[0] != ENTRY_DELETED) {
				print_info(&entries[i]);
				if ((entries + i)->attributes & ATTR_DIRECTORY)
					++dirs;
				else {
					++files;
					size_of_files += (entries + i)->size_of_file;
				}
			}
		}
	}
	else {
		entries = (dir_entry*)malloc(boot.bytes0_35.bytes_per_sector * boot.bytes0_35.sectors_per_cluster);
		if (!entries) {
			return 2;
		}
		bool finish = false;
		uint16_t cluster = directory.back().entry.first_cluster_2;
		do {
			if (readblock(entries, cluster_start(cluster), boot.bytes0_35.sectors_per_cluster) != boot.bytes0_35.sectors_per_cluster) {
				free(entries);
				return 1;
			}
			for (unsigned i = 0; i < boot.bytes0_35.bytes_per_sector * boot.bytes0_35.sectors_per_cluster / sizeof(dir_entry); ++i) {
				if ((entries + i)->filename[0] == ENTRY_LAST) {
					finish = true;
					break;
				}
				if ((entries + i)->filename[0] != ENTRY_DELETED && (entries + i)->filename[0] != '.') {
					print_info(&entries[i]);
					if ((entries + i)->attributes & ATTR_DIRECTORY)
						++dirs;
					else {
						++files;
						size_of_files += (entries + i)->size_of_file;
					}
				}
			}
			if (!finish) {
				cluster = next_cluster(cluster);
				if (cluster >= EOC1 && cluster <= EOC2)
					finish = true;
			}
		} while (!finish);
	}
	free(entries);
	uint16_t cluster = 0;
	int free_clusters = 0;
	uint8_t* alloctab = (uint8_t*)malloc(boot.bytes0_35.bytes_per_sector * boot.bytes0_35.sectors_per_fat * sizeof(uint8_t));
	if (!alloctab) {
		return 2;
	}
	if (readblock(alloctab, fat1_start, boot.bytes0_35.sectors_per_fat) != boot.bytes0_35.sectors_per_fat) {
		free(alloctab);
		return 1;
	}
	uint32_t offset = 0;
	uint32_t fat_entry_count = available_clusters + 2;
	for (uint32_t i = 2; i < fat_entry_count; i += 1) { //2 to skip reserved clusters
		offset = 3 * (i / 2);
		if (i % 2 != 0)
			cluster = *(alloctab + offset + 2) << 4 | (*(alloctab + offset + 1) & 0xF0) >> 4;
		else
			cluster = (*(alloctab + offset + 1) & 0x0F) << 8 | *(alloctab + offset);
		if (cluster == CLUSTER_FREE)
			++free_clusters;
	}
	printf("%10d File(s) %12lu bytes\n", files, size_of_files);
	printf("%10d Dir(s) %13lu bytes free\n", dirs, (lba_t)free_clusters * boot.bytes0_35.sectors_per_cluster * boot.bytes0_35.bytes_per_sector);
	free(alloctab);
	return 0;
}

void cd(char* name) {
	if (!name || !strcmp(name, "."))
		return;
	if (!strcmp(name, "..")) {
		if (!directory.empty())
			directory.pop_back();
		return;
	}
	if (!strcmp(name, "/")) {
		directory.clear();
		return;
	}
	dir_entry* entry = find_entry(name, DIR);
	if (entry) {
		dir_info dir_i;
		dir_i.path = (char*)entry->filename;
		dir_i.path.resize(strlen(name));
		dir_i.entry = *entry;
		directory.push_back(dir_i);
		free(entry);
		return;
	}
	cout << "cd: " << name << ": No such directory" << endl;
}

void pwd() {
	cout << "Current directory: ";
	if (directory.empty())
		cout << "\\";
	for (unsigned int i = 0; i < directory.size();++i)
		cout << "\\" << directory.at(i).path;
	cout << endl;
}

void cat(char* name) {
	if (name) {
		string temp = name;
		FILE_FAT* file = open_fat(name);
		if (!file) {
			cout << "cat: " << temp << ": No such file" << endl;
			return;
		}
		if (!file->size) {
			close_fat(file);
			return;
		}
		char* data = (char*)calloc(file->size + 1, sizeof(char));
		if (!data) {
			close_fat(file);
			return;
		}
		if (read_fat(file, data) != file->size) {
			close_fat(file);
			free(data);
			return;
		}
		printf("%s\n", data);
		close_fat(file);
		free(data);
	}
}

int get(char* name) {
	if (!name)
		return 1;
	FILE* test = fopen(name, "r");
	if (test) {
		fclose(test);
		return 7;
	}
	char* name_copy = (char*)calloc(strlen(name) + 1, sizeof(char));
	if (!name_copy)
		return 3;
	strcpy(name_copy, name);
	FILE_FAT* input = open_fat(name);
	if (!input) {
		free(name_copy);
		return 2;
	}
	char* data = (char*)calloc(input->size + 1, sizeof(char));
	if (!data) {
		free(name_copy);
		close_fat(input);
		return 3;
	}
	read_fat(input, data);
	FILE* output = fopen(name_copy, "w");
	if (!output) {
		close_fat(input);
		free(name_copy);
		free(data);
		return 4;
	}
	fwrite(data, sizeof(char), input->size, output);
	free(name_copy);
	free(data);
	close_fat(input);
	fclose(output);
	return 0;
}

int zip(char* name1, char* name2, char* name3) {
	if (!name1 || !name2 || !name3)
		return 1;
	FILE* test = fopen(name3, "r");
	if (test) {
		fclose(test);
		return 7;
	}
	FILE_FAT* input1 = open_fat(name1);
	if (!input1) {
		return 2;
	}
	char* data1 = (char*)calloc(input1->size + 1, sizeof(char));
	if (!data1) {
		close_fat(input1);
		return 3;
	}
	read_fat(input1, data1);
	FILE_FAT* input2 = open_fat(name2);
	if (!input2) {
		close_fat(input1);
		free(data1);
		return 5;
	}
	char* data2 = (char*)calloc(input2->size + 1, sizeof(char));
	if (!data2) {
		close_fat(input1);
		close_fat(input2);
		free(data1);
		return 3;
	}
	read_fat(input2, data2);
	FILE* output = fopen(name3, "a");
	if (!output) {
		close_fat(input1);
		close_fat(input2);
		free(data1);
		free(data2);
		return 4;
	}
	bool f1_done = false, f2_done = false;
	if (!input1->size)
		f1_done = true;
	if (!input2->size)
		f2_done = true;
	int i = 0, j = 0;
	while (!f1_done || !f2_done) {
		while (!f1_done) {
			if (!*(data1 + i)) {
				fwrite("\n", sizeof(char), 1, output);
				f1_done = true;
				break;
			}
			fwrite(data1 + i, sizeof(char), 1, output);
			if (*(data1 + i) == '\n') {
				++i;
				break;
			}
			i++;
		}
		while (!f2_done) {
			if (!*(data2 + j)) {
				fwrite("\n", sizeof(char), 1, output);
				f2_done = true;
				break;
			}
			fwrite(data2 + j, sizeof(char), 1, output);
			if (*(data2 + j) == '\n') {
				++j;
				break;
			}
			j++;
		}
	}
	free(data1);
	free(data2);
	close_fat(input1);
	close_fat(input2);
	fclose(output);
	return 0;
}

void rootinfo() {
	int sum = 0;
	dir_entry* entries = (dir_entry*)malloc(boot.bytes0_35.root_dir_capacity * sizeof(dir_entry));
	if (!entries) {
		return;
	}
	if (readblock(entries, root_start, sectors_per_root) != sectors_per_root) {
		free(entries);
		return;
	}
	for (unsigned i = 0; (entries + i)->filename[0] != ENTRY_LAST; ++i) {
		if ((entries + i)->filename[0] != ENTRY_DELETED)
			++sum;
	}
	printf("Number of entries in the root directory: %u\n", sum);
	printf("Maximum number of entries in the root directory: %u\n", boot.bytes0_35.root_dir_capacity);
	printf("Percentage fill of the root directory: %.2f%%\n", 100 * (float)sum / boot.bytes0_35.root_dir_capacity);
	free(entries);
}
void spaceinfo() {
	uint16_t cluster = 0;
	int damaged_clusters = 0, free_clusters = 0, used_clusters = 0, last_clusters = 0;
	uint8_t* alloctab = (uint8_t*)malloc(boot.bytes0_35.bytes_per_sector * boot.bytes0_35.sectors_per_fat * sizeof(uint8_t));
	if (!alloctab) {
		return;
	}
	if (readblock(alloctab, fat1_start, boot.bytes0_35.sectors_per_fat) != boot.bytes0_35.sectors_per_fat) {
		free(alloctab);
		return;
	}
	uint32_t offset = 0;
	uint32_t fat_entry_count = available_clusters + 2;
	for (uint32_t i = 2; i < fat_entry_count; i += 1) { //2 to skip reserved clusters
		offset = 3 * (i / 2);
		if (i % 2 != 0)
			cluster = *(alloctab + offset + 2) << 4 | (*(alloctab + offset + 1) & 0xF0) >> 4;
		else
			cluster = (*(alloctab + offset + 1) & 0x0F) << 8 | *(alloctab + offset);
		if (cluster == CLUSTER_DAMAGED)
			++damaged_clusters;
		if (cluster == CLUSTER_FREE)
			++free_clusters;
		if (cluster >= EOC1 && cluster <= EOC2)
			++last_clusters;
		if (cluster >= CLUSTER_USED && cluster <= CLUSTER_USED2)
			++used_clusters;
	}
	printf("Number of used clusters: %d\n", used_clusters);
	printf("Number of free clusters: %d\n", free_clusters);
	printf("Number of damaged clusters: %d\n", damaged_clusters);
	printf("Number of last clusters (ends of cluster chains): %d\n", last_clusters);
	printf("Size of cluster: %u(in bytes), %u(in sectors)\n", boot.bytes0_35.bytes_per_sector * boot.bytes0_35.sectors_per_cluster, boot.bytes0_35.sectors_per_cluster);
	free(alloctab);
}
void fileinfo(char* name) {
	if (!name)
		return;
	dir_entry* entry = find_entry(name, IGNORE);
	if (!entry) {
		printf("fileinfo: No such file or directory\n");
		return;
	}
	printf("Full name: ");
	for (unsigned int i = 0; i < directory.size();++i)
		cout << "\\" << directory.at(i).path;
	cout << "\\";
	for (unsigned i = 0;*(entry->filename + i) != ' ';++i)
		printf("%c", *(entry->filename + i));
	if (*entry->extension != ' ')
		printf(".%s", entry->extension);
	printf("\nAttributes: A%c R%c S%c H%c D%c V%c", entry->attributes & ATTR_ARCHIVE ? '+' : '-', entry->attributes & ATTR_READONLY ? '+' : '-', entry->attributes & ATTR_SYSTEM ? '+' : '-', entry->attributes & ATTR_HIDDEN ? '+' : '-', entry->attributes & ATTR_DIRECTORY ? '+' : '-', entry->attributes & ATTR_VOLUME ? '+' : '-');
	printf("\nSize: %u bytes", entry->size_of_file);
	printf("\nLast modified: %02u/%02u/%u  ", (uint16_t)(entry->modify_date << 11) >> 11, (uint16_t)(entry->modify_date << 7) >> 12, (uint16_t)(entry->modify_date >> 9) + 1980);
	printf("%02u:%02u", (uint16_t)(entry->modify_time >> 11), (uint16_t)(entry->modify_time << 5) >> 10);
	printf("\nLast access: %02u/%02u/%u  ", (uint16_t)(entry->last_access_date << 11) >> 11, (uint16_t)(entry->last_access_date << 7) >> 12, (uint16_t)(entry->last_access_date >> 9) + 1980);
	printf("\nCreated: %02u/%02u/%u  ", (uint16_t)(entry->creation_date << 11) >> 11, (uint16_t)(entry->creation_date << 7) >> 12, (uint16_t)(entry->creation_date >> 9) + 1980);
	printf("%02u:%02u", (uint16_t)(entry->creation_time >> 11), (uint16_t)(entry->creation_time << 5) >> 10);
	printf("\nChain of clusters: [%u]", entry->first_cluster_2);
	uint16_t cluster = entry->first_cluster_2;
	int clusters_count = 1;
	while (1) {
		cluster = next_cluster(cluster);
		if (cluster < EOC1 || cluster > EOC2) {
			printf(", %u", cluster);
			++clusters_count;
		}
		else
			break;
	}
	printf("\nClusters: %d\n", clusters_count);
	free(entry);
}

dir_entry* find_entry(char* name, ENTRY_TYPE type) {
	if (!name)
		return NULL;
	char* filename = strtok(name, ".");
	char* ext = strtok(NULL, "");
	dir_entry* entries;
    if(!filename){
        return NULL;
    }
	if (directory.empty()) {
		entries = (dir_entry*)malloc(boot.bytes0_35.root_dir_capacity * sizeof(dir_entry));
		if (!entries) {
			return NULL;
		}
		if (readblock(entries, root_start, sectors_per_root) != sectors_per_root) {
			free(entries);
			return NULL;
		}
		for (unsigned i = 0; (entries + i)->filename[0] != ENTRY_LAST; ++i) {
			if ((entries + i)->filename[0] != ENTRY_DELETED && strlen(filename) == my_strlen((const char*)(entries + i)->filename) && !strncasecmp((const char*)(entries + i)->filename, filename, my_strlen((const char*)(entries + i)->filename))) {
				if (type == FIL && (entries + i)->attributes & ATTR_DIRECTORY)
					continue;
				else if (type == DIR && !((entries + i)->attributes & ATTR_DIRECTORY))
					continue;
				if ((entries + i)->extension[0] != ' ') {
					if (!ext)
						continue;
					if (strlen(ext) == my_strlen((const char*)(entries + i)->extension) && !strncasecmp((const char*)(entries + i)->extension, ext, my_strlen((const char*)(entries + i)->extension))) {
						dir_entry* found = (dir_entry*)malloc(sizeof(dir_entry));
						if (!found) {
							free(entries);
							return NULL;
						}
						memcpy(found, entries + i, sizeof(dir_entry));
						free(entries);
						return found;
					}
				}
				else {
					dir_entry* found = (dir_entry*)malloc(sizeof(dir_entry));
					if (!found) {
						free(entries);
						return NULL;
					}
					memcpy(found, entries + i, sizeof(dir_entry));
					free(entries);
					return found;
				}
			}
		}
	}
	else {
		entries = (dir_entry*)malloc(boot.bytes0_35.bytes_per_sector * boot.bytes0_35.sectors_per_cluster);
		if (!entries) {
			return NULL;
		}
		bool finish = false;
		uint16_t cluster = directory.back().entry.first_cluster_2;
		do {
			if (readblock(entries, cluster_start(cluster), boot.bytes0_35.sectors_per_cluster) != boot.bytes0_35.sectors_per_cluster) {
				free(entries);
				return NULL;
			}
			for (unsigned i = 0; i < boot.bytes0_35.bytes_per_sector * boot.bytes0_35.sectors_per_cluster / sizeof(dir_entry); ++i) {
				if ((entries + i)->filename[0] == ENTRY_LAST) {
					finish = true;
					break;
				}

				if ((entries + i)->filename[0] != ENTRY_DELETED && strlen(filename) == my_strlen((const char*)(entries + i)->filename) && !strncasecmp((const char*)(entries + i)->filename, filename, my_strlen((const char*)(entries + i)->filename))) {
					if (type == FIL && (entries + i)->attributes & ATTR_DIRECTORY)
						continue;
					else if (type == DIR && !((entries + i)->attributes & ATTR_DIRECTORY))
						continue;
					if ((entries + i)->extension[0] != ' ') {
						if (!ext)
							continue;
						if (strlen(ext) == my_strlen((const char*)(entries + i)->extension) && !strncasecmp((const char*)(entries + i)->extension, ext, my_strlen((const char*)(entries + i)->extension))) {
							dir_entry* found = (dir_entry*)malloc(sizeof(dir_entry));
							if (!found) {
								free(entries);
								return NULL;
							}
							memcpy(found, entries + i, sizeof(dir_entry));
							free(entries);
							return found;
						}
					}
					else {
						dir_entry* found = (dir_entry*)malloc(sizeof(dir_entry));
						if (!found) {
							free(entries);
							return NULL;
						}
						memcpy(found, entries + i, sizeof(dir_entry));
						free(entries);
						return found;
					}
				}
			}
			if (!finish) {
				cluster = next_cluster(cluster);
				if (cluster >= EOC1 && cluster <= EOC2) {
					finish = true;
					break;
				}
			}
		} while (!finish);
	}
	free(entries);
	return NULL;
}

FILE_FAT* open_fat(char* name) {
	if (!name)
		return NULL;
	dir_entry* entry = find_entry(name, FIL);
	if (!entry)
		return NULL;
	FILE_FAT* file = (FILE_FAT*)calloc(sizeof(FILE_FAT), 1);
	if (!file)
		return NULL;
	file->current_index = 0;
	file->first_cluster = entry->first_cluster_2;
	file->size = entry->size_of_file;
	free(entry);
	return file;
}
void close_fat(FILE_FAT* file) {
	if (file)
		free(file);
}
size_t read_fat(FILE_FAT* file, void* buf) {
	if (!file || !buf)
		return 0;
	uint8_t* temp = (uint8_t*)malloc(boot.bytes0_35.sectors_per_cluster * boot.bytes0_35.bytes_per_sector);
	if (!temp)
		return 0;
	size_t offset = 0;
	uint16_t cluster = file->first_cluster;
	size_t to_read = file->size;
	do {
		if (!readblock(temp, cluster_start(cluster), boot.bytes0_35.sectors_per_cluster)) {
			free(temp);
			return 0;
		}

		if (boot.bytes0_35.sectors_per_cluster * boot.bytes0_35.bytes_per_sector <= to_read) {
			memcpy((uint8_t*)buf + offset, temp, boot.bytes0_35.sectors_per_cluster * boot.bytes0_35.bytes_per_sector);
			to_read -= boot.bytes0_35.sectors_per_cluster * boot.bytes0_35.bytes_per_sector;
			offset += boot.bytes0_35.sectors_per_cluster * boot.bytes0_35.bytes_per_sector;
		}
		else {
			memcpy((uint8_t*)buf + offset, temp, to_read);
			offset += to_read;
			to_read = 0;
		}
		cluster = next_cluster(cluster);
		if (cluster >= EOC1 && cluster <= EOC2)
			break;
	} while (1);
	free(temp);
	return offset;
}

size_t my_strlen(const char* text) {
	size_t size = 0;
	while (*(text + size++) != ' ');
	return size - 1;
}

void print_info(const dir_entry* entry) {
	printf("%02u/%02u/%u  ", (uint16_t)(entry->modify_date << 11) >> 11, (uint16_t)(entry->modify_date << 7) >> 12, (uint16_t)(entry->modify_date >> 9) + 1980);
	printf("%02u:%02u", (uint16_t)(entry->modify_time >> 11), (uint16_t)(entry->modify_time << 5) >> 10);
	if (entry->attributes == ATTR_DIRECTORY) {
		printf("   <DIRECTORY>   %s", entry->filename);
	}
	else {
		printf("   %13u ", entry->size_of_file);
		for (unsigned i = 0;*(entry->filename + i) != ' ';++i)
			printf("%c", *(entry->filename + i));
		if (*entry->extension != ' ')
			printf(".%s", entry->extension);
	}
	printf("\n");
}

void fatview() {
	cout << "fatview:";
	if (directory.empty())
		cout << "/";
	for (unsigned int i = 0; i < directory.size();++i)
		cout << "/" << directory.at(i).path;
	cout << "# ";
}

uint16_t next_cluster(uint16_t cluster) {
	uint32_t offset = 3 * (cluster / 2);
	uint8_t* alloctab = (uint8_t*)malloc(boot.bytes0_35.bytes_per_sector * boot.bytes0_35.sectors_per_fat * sizeof(uint8_t));
	if (!alloctab) {
		return EOC2;
	}
	if (readblock(alloctab, fat1_start, boot.bytes0_35.sectors_per_fat) != boot.bytes0_35.sectors_per_fat) {
		free(alloctab);
		return EOC2;
	}
	if (cluster % 2 != 0)
		cluster = *(alloctab + offset + 2) << 4 | (*(alloctab + offset + 1) & 0xF0) >> 4;
	else 
		cluster = (*(alloctab + offset + 1) & 0x0F) << 8 | *(alloctab + offset);
	free(alloctab);
	return cluster;
}
bool validate_fat() {
    bool ok = false;
    for (int i=0; i<8; ++i){
        if(boot.bytes0_35.sectors_per_cluster == pow(2,i)){
            ok = true;
            break;
        }
    }
	if (ok && boot.bytes0_35.fat_count >= 1 && boot.bytes0_35.fat_count <= 2 && boot.bytes0_35.reserved_sectors > 0 && ((boot.bytes0_35.logical_sectors32 == 0) ^ (boot.bytes0_35.logical_sectors16 == 0)) && boot.bytes0_35.root_dir_capacity * sizeof(dir_entry) % boot.bytes0_35.bytes_per_sector == 0)
		return true;
	return false;
}