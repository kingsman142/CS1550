/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;

typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE)

struct cs1550_disk_block
{
	//All of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

#define MAX_FAT_ENTRIES (BLOCK_SIZE/sizeof(short))

struct cs1550_file_alloc_table_block {
	short table[MAX_FAT_ENTRIES];
};

typedef struct cs1550_file_alloc_table_block cs1550_fat_block;

#define START_ALLOC_BLOCK 2 //block 0 = root; block 1 = FAT; start allocation of directories and files at block 2 in the allocation table

static cs1550_root_directory read_root(void);
static cs1550_fat_block read_fat(void);

//Read root from disk
static cs1550_root_directory read_root(){
	FILE* disk = fopen(".disk", "r+b");
	fseek(disk, 0, SEEK_SET);
	cs1550_root_directory root;
	fread(&root, BLOCK_SIZE, 1, disk);
	return root;
}

//Read the FAT from disk
static cs1550_fat_block read_fat(){
	FILE* disk = fopen(".disk", "r+b");
	fseek(disk, BLOCK_SIZE, SEEK_SET);
	cs1550_fat_block fat;
	fread(&fat, BLOCK_SIZE, 1, disk);
	return fat;
}

//Write the root data from a given pointer to disk at block 0
static void write_root(cs1550_root_directory* root_on_disk){
	FILE* disk = fopen(".disk", "r+b");
	fwrite(root_on_disk, BLOCK_SIZE, 1, disk);
	fclose(disk);
}

//Write the FAT data from a given pointer to disk at block 1
static void write_fat(cs1550_fat_block* fat_on_disk){
	FILE* disk = fopen(".disk", "r+b");
	fseek(disk, BLOCK_SIZE, SEEK_SET);
	fwrite(fat_on_disk, BLOCK_SIZE, 1, disk);
	fclose(disk);
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	//Store the path data for easy navigation later on
	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_EXTENSION+1];
	strcpy(directory, "");
	strcpy(filename, "");
	strcpy(extension, "");

	if(strlen(path) != 1){ //If the path is not just "/", parse the necessary directory, filename, and extension if possible
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	}	

	//If any of the variables are longer than the 8.3 file naming convention, return the ENAMETOOLONG error
	if(strlen(directory) > MAX_FILENAME || strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION){
		return -ENAMETOOLONG;
	}

	//Clear the data in stbuf JUST IN CASE
	memset(stbuf, 0, sizeof(struct stat));
 
	//The path is just the root directory
	if(strcmp(path, "/") == 0){
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		res = 0;
		return res;
	} else{ //Navigate through the file system to find the correct directory and/or file
		if(strcmp(directory, "") == 0){ //If the directory is empty, return that the file cannot be found
			res = -ENOENT;
			return res;
		} else{
			int i = 0;
			struct cs1550_directory dir;
			strcpy(dir.dname, "");
			dir.nStartBlock = -1;
			
			cs1550_root_directory root = read_root();

			for(i = 0; i < root.nDirectories; i++){ //Find the subdirectory in the root's array of directories
				struct cs1550_directory curr_dir = root.directories[i];
				if(strcmp(curr_dir.dname, directory) == 0){ //This current directory and our search directory names match
					dir = curr_dir;
					break;
				}
			}

			if(strcmp(dir.dname, "") == 0){ //No directory was found, so return an ENOENT error
				res = -ENOENT;
				return res;
			}

			if(strcmp(filename, "") == 0){ //No more left in the path to traverse; the user was only looking to get the attributes of a directory
				res = 0;
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2;
				return res; //Return a success
			}

			FILE* disk = fopen(".disk", "r+b");
			int location_on_disk = BLOCK_SIZE*dir.nStartBlock;
			fseek(disk, location_on_disk, SEEK_SET);
	
			cs1550_directory_entry dir_entry;
			dir_entry.nFiles = 0;
			memset(dir_entry.files, 0, MAX_FILES_IN_DIR*sizeof(struct cs1550_file_directory));

			int num_items_successfully_read = fread(&dir_entry, BLOCK_SIZE, 1, disk); //Read in the directory's data, such as its files contained within
			fclose(disk);
	
			if(num_items_successfully_read == 1){ //One block was successfully read, so proceed
				struct cs1550_file_directory file;
				strcpy(file.fname, "");
				strcpy(file.fext, "");
				file.fsize = 0;
				file.nStartBlock = -1;
				
				int i = 0;
				for(i = 0; i < MAX_FILES_IN_DIR; i++){ //Iterate over the files in the directory
					struct cs1550_file_directory curr_file = dir_entry.files[i];
					if(strcmp(curr_file.fname, filename) == 0 && strcmp(curr_file.fext, extension) == 0){ //Both the current filename and file extension match
														    //the filename and file extension we're looking for.
						file = curr_file;
						break;
					}
				}

				if((file.nStartBlock) == -1){ //No file was found, so return a file not found error
					res = -ENOENT;
					return res;
				} else{ //The file we were looking for was found!
					res = 0;
					stbuf->st_mode = S_IFREG | 0666;
					stbuf->st_nlink = 1;
					stbuf->st_size = file.fsize;
					return res; //Return success
				}
			}
		}
	}
	
	return res;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;
	
	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	//Parse the path, which is in the form: root/destination/filename.extension
	int path_length = strlen(path);
	char path_copy[path_length];
	strcpy(path_copy, path);	

	char* destination = strtok(path_copy, "/");
	char* filename = strtok(NULL, ".");
	char* extension = strtok(NULL, ".");

	//Each of these iff statements first checks if the string is null; if not, check the length.
	//If the length is longer than the 8.3 file naming convention we're using, return the error ENAMETOOLONG.
	if((destination && destination[0]) && strlen(destination) > MAX_FILENAME){
		return -ENAMETOOLONG;
	}
	if((filename && filename[0]) && strlen(filename) > MAX_FILENAME){
		return -ENAMETOOLONG;
	}
	if((extension && extension[0]) && strlen(extension) > MAX_EXTENSION){
		return -ENAMETOOLONG;
	}

	//Enter the root
	if(strcmp(path, "/") == 0){
		int i = 0;
		
		cs1550_root_directory root = read_root();

		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){ //Iterate over all of the directories in the root; if their name is non-empty, print for the user using filler()
			char* directory_name = root.directories[i].dname;
			if(strcmp(directory_name, "") != 0){
				filler(buf, directory_name, NULL, 0);
			}
		}

		return 0;
	} else{
		int i = 0;
		struct cs1550_directory dir; //Initialize a directory for file checking later on
		strcpy(dir.dname, "");
		dir.nStartBlock = -1;

		cs1550_root_directory root = read_root();

		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){ //Iterate over the directories in the root until we find the directory with a matching name with the path
			if(strcmp(destination, root.directories[i].dname) == 0){
				dir = root.directories[i];
				break;
			}
		}

		if(strcmp(dir.dname, "") == 0){ //No directory was found in the root, so return file not found error
			return -ENOENT;
		} else{ //The proper directory was found, so read the directory
			FILE* disk = fopen(".disk", "rb+");
			int location_on_disk = dir.nStartBlock*BLOCK_SIZE;
			fseek(disk, location_on_disk, SEEK_SET);

			cs1550_directory_entry directory;
			directory.nFiles = 0;
			memset(directory.files, 0, MAX_FILES_IN_DIR*sizeof(struct cs1550_file_directory));

			fread(&directory, BLOCK_SIZE, 1, disk); //Read the directory data from memory to iterate over its files
			fclose(disk);

			int j = 0;
			for(j = 0; j < MAX_FILES_IN_DIR; j++){ //Iterate over the non-empty filenames in this directory and print them to the user using filler()
				struct cs1550_file_directory file_dir = directory.files[j];
				char filename_copy[MAX_FILENAME+1];
				strcpy(filename_copy, file_dir.fname);
				if(strcmp(file_dir.fext, "") != 0){ //Append the file extension
					strcat(filename_copy, ".");
				}
				strcat(filename_copy, file_dir.fext); //Append file extension
				if(strcmp(file_dir.fname, "") != 0){ //If the file is not empty, add it to the filler buffer
					filler(buf, filename_copy, NULL, 0);
				}
			}
		}
	}

	return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;

	//path will be in the format of /directory/sub_directory
	char* directory; //The first directory in the 2-level file system
	char* sub_directory; //The directory within the root's directory

	//Parse the two strings
	int path_length = strlen(path);
	char path_copy[path_length];
	strcpy(path_copy, path);	

	directory = strtok(path_copy, "/");
	sub_directory = strtok(NULL, "/"); //NULL indicates to continue where strtok left off at

	if(strlen(directory) > MAX_FILENAME){ //The main directory (e.g. /exampleFileNameHere/... is too long; max of 8 characters
		return -ENAMETOOLONG;
	} else if(sub_directory && sub_directory[0]){ //The user passed in a sub directory; this is illegal in our two-level file system
						      //because the second level should only be files.  Return that permission was denied.
		return -EPERM;
	}

	cs1550_root_directory root = read_root();
	cs1550_fat_block fat = read_fat();

	if(root.nDirectories >= MAX_DIRS_IN_ROOT){
		return -EPERM; //Can't add anymore directories
	}

	int h = 0;
	for(h = 0; h < MAX_DIRS_IN_ROOT; h++){ //Scan through the directories in the root; if any match the directory we're trying to create,
					       //inform the user that that directory already exists.
		if(strcmp(root.directories[h].dname, directory) == 0){
			return -EEXIST;
		}
	}

	int i = 0;
	for(i = 0; i < MAX_DIRS_IN_ROOT; i++){ //Iterate through the root's directories/folders
		if(strcmp(root.directories[i].dname, "") == 0){ //If this folder is nameless (it doesn't exist yet), use it to create a new directory
			struct cs1550_directory new_dir_in_root;
			strcpy(new_dir_in_root.dname, directory); //Copy the user's new directory name into this struct
			
			int j = 0;
			for(j = START_ALLOC_BLOCK; j < MAX_FAT_ENTRIES; j++){ //Iterate over the FAT to find a new block to store the directory in
									      //NOTE: j starts at 2 because on the disk:
									      //	index 0 = root
									      //	index 1 = FAT
				if(fat.table[j] == 0){ //Currently nothing allocated at this index
					fat.table[j] = EOF; //Directory only requires 1 block
					new_dir_in_root.nStartBlock = j;
					break;
				}
			}

			FILE* disk = fopen(".disk", "r+b");
			int location_on_disk = BLOCK_SIZE*new_dir_in_root.nStartBlock; //The location on the disk for this directory is the starting block * 512
			fseek(disk, location_on_disk, SEEK_SET);
			cs1550_directory_entry dir;
			dir.nFiles = 0; //Directory begins with 0 files in it
			
			int num_items_read = fread(&dir, BLOCK_SIZE, 1, disk);
			
			if(num_items_read == 1){ //fread returned successfully with 1 item
				memset(&dir, 0, sizeof(struct cs1550_directory_entry)); //Clear the directory data we just read in JUST IN CASE
				fwrite(&dir, BLOCK_SIZE, 1, disk); //Write the new directory data
				fclose(disk);

				//Update the root with its new data and write it to disk, as well as the FAT
				root.nDirectories++;
				root.directories[i] = new_dir_in_root;				

				write_root(&root);
				write_fat(&fat);
			} else{ //There was an error reading in the data from disk, so just close the file
				fclose(disk);
			}

			return 0;
		}


	}

	return 0; //Return success since no errors occurred by this point
}

/* 
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;

	//path will be in the format of /directory/sub_directory
	char* directory; //The first directory in the 2-level file system
	char* file_name; //The directory within the root's directory	
	char* file_ext;

	//Parse the two strings
	int path_length = strlen(path);
	char path_copy[path_length];
	strcpy(path_copy, path);	

	directory = strtok(path_copy, "/");
	file_name = strtok(NULL, "."); //NULL indicates to continue where strtok left off at
	file_ext = strtok(NULL, ".");

	if((directory && directory[0]) && strcmp(directory, "") != 0){ //Directory and filename are not empty, so search for it
		if(file_name && file_name[0]){ //filename NULL check
			if(strcmp(file_name, "") == 0){ //filename is empty
				return -EPERM; //Can't create a file in the root directory
			}

			if(file_ext && file_ext[0]){ //file extension NULL check
				if(strlen(file_name) > MAX_FILENAME || strlen(file_ext) > MAX_EXTENSION){ //filename or extension is longer than the 8.3 format
					return -ENAMETOOLONG;
				}
			} else{ //filename is not null, but file extensin is
				if(strlen(file_name) > MAX_FILENAME){ //filename is longer than 8 characters
					return -ENAMETOOLONG;
				}
			}
		} else{ //filename is null, so only directory was given
			return -EPERM; //Can't create in the root directory
		}

		//Read in the root and FAT so we can grab their data
		cs1550_root_directory root = read_root();
		cs1550_fat_block fat = read_fat();

		struct cs1550_directory dir;

		int i = 0;
		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){ //Iterate over the directories in the root to find the correct directory
			struct cs1550_directory curr_dir = root.directories[i];
			if(strcmp(directory, curr_dir.dname) == 0){ //Found a matching directory!
				dir = curr_dir;
				break;
			}
		}

		if(strcmp(dir.dname, "") != 0){ //Valid directory was found
			//Read in the directory from disk
			long dir_location_on_disk = BLOCK_SIZE*dir.nStartBlock;
			FILE* disk = fopen(".disk", "r+b");
			fseek(disk, dir_location_on_disk, SEEK_SET);
			
			cs1550_directory_entry dir_entry;
			int success = fread(&dir_entry, BLOCK_SIZE, 1, disk);

			if(dir_entry.nFiles >= MAX_FILES_IN_DIR){
				return -EPERM; //Can't create more files than allowed in a directory
			}
	
			if(success){ //We can add another file to this directory
				int file_already_exists = 0;
				int first_free_file_dir_index = -1;

				int j = 0;
				for(j = 0; j < MAX_FILES_IN_DIR; j++){ //Iterate over the files in this directory to make sure the file doesn't already exist
					struct cs1550_file_directory curr_file_dir = dir_entry.files[j];
					if(strcmp(curr_file_dir.fname, "") == 0 && strcmp(curr_file_dir.fext, "") == 0 && first_free_file_dir_index == -1){ //Keep track of this later so we can easily add the new file to this array index
						first_free_file_dir_index = j;
					}

					if(strcmp(curr_file_dir.fname, file_name) == 0 && strcmp(curr_file_dir.fext, file_ext) == 0){ //Found a file with the same filename and extension; abort!
						file_already_exists = 1;
						break;
					}			
				}

				if(!file_already_exists){ //File doesn't exist already
					short file_fat_start_index = -1;

					int k = 0;
					for(k = 2; k < MAX_FAT_ENTRIES; k++){ //Allocate new block in the FAT for this file
						if(fat.table[k] == 0){
							file_fat_start_index = k;
							fat.table[k] = EOF;
							break;
						}
					}

					struct cs1550_file_directory new_file_dir;
					strcpy(new_file_dir.fname, file_name);
					if(file_ext && file_ext[0]) strcpy(new_file_dir.fext, file_ext); //Add the file extension to the file entry
					else strcpy(new_file_dir.fext, ""); //Make the extension blank if none was given
					new_file_dir.fsize = 0; //Initialize file size to 0
					new_file_dir.nStartBlock = file_fat_start_index;
					
					//Remember that index we kept track of later?  We can easily insert the new file at that index
					dir_entry.files[first_free_file_dir_index] = new_file_dir;
					dir_entry.nFiles++; //This directory has 1 more file in it
					
					//Write the directory data back to disk
					fseek(disk, dir_location_on_disk, SEEK_SET);
					fwrite(&dir_entry, BLOCK_SIZE, 1, disk);

					fclose(disk);
					
					//Write the root and FAT back to disk
					write_root(&root);
					write_fat(&fat);
				} else{ //File already exists, so no permissions are given to add another one
					fclose(disk);
					return -EEXIST;
				}
			} else{ //Directory name is empty, so can't add a new file
				fclose(disk);
				return -EPERM;
			}
		} else{ //Directory string was null or empty
			if(strcmp(directory, "") == 0){
				return 0;
			} else if(strcmp(file_name, "") == 0){
				return -EPERM;
			}
		}		
	}	

	return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	//path will be in the format of /directory/sub_directory
	char* directory; //The first directory in the 2-level file system
	char* file_name; //The directory within the root's directory	
	char* file_ext;

	//Parse the two strings
	int path_length = strlen(path);
	char path_copy[path_length];
	strcpy(path_copy, path);	

	directory = strtok(path_copy, "/");
	file_name = strtok(NULL, "."); //NULL indicates to continue where strtok left off at
	file_ext = strtok(NULL, ".");

	if((directory && directory[0]) && strcmp(directory, "") != 0){ //Directory and filename are not empty, so search for it
		if(file_name && file_name[0]){ //filename NULL check
			if(strcmp(file_name, "") == 0){ //filename is empty
				return -EEXIST; //Can't read a file in the root directory
			}

			if(file_ext && file_ext[0]){ //file extension NULL check
				//Check if filename or extension are too long
				if(strlen(file_name) > MAX_FILENAME || strlen(file_ext) > MAX_EXTENSION){
					return -ENAMETOOLONG;
				}
			} else{
				//Check if filename is too long
				if(strlen(file_name) > MAX_FILENAME){
					return -ENAMETOOLONG;
				}
			}
		} else{ //filename is NULL
			return -EEXIST; //Can't read a file in the root directory
		}

		//Read in the root and FAT
		cs1550_root_directory root = read_root();
		cs1550_fat_block fat = read_fat();

		struct cs1550_directory dir;

		int i = 0;
		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){ //Iterate over the directories in the root to find the respective directory
			struct cs1550_directory curr_dir = root.directories[i];
			if(strcmp(directory, curr_dir.dname) == 0){ //Found a matching directory!
				dir = curr_dir;
				break;
			}
		}

		if(strcmp(dir.dname, "") != 0){ //Valid directory was found
			//Read in the directory entry from disk
			long dir_location_on_disk = BLOCK_SIZE*dir.nStartBlock;
			FILE* disk = fopen(".disk", "r+b");
			fseek(disk, dir_location_on_disk, SEEK_SET);
			
			cs1550_directory_entry dir_entry;
			int success = fread(&dir_entry, BLOCK_SIZE, 1, disk);

			fclose(disk);
	
			if(success){ //One directory entry was read in
				struct cs1550_file_directory file_dir;

				int j = 0;
				for(j = 0; j < MAX_FILES_IN_DIR; j++){ //Search through this directory to find the file we're looking for
					struct cs1550_file_directory curr_file_dir = dir_entry.files[j];

					if(strcmp(curr_file_dir.fname, file_name) == 0){ //Matching filename
						if(file_ext && file_ext[0]){ //File extension null check
							if(strcmp(curr_file_dir.fext, file_ext) == 0){ //Matching file extension, so we found the file!
								file_dir = curr_file_dir;
								break;
							}
						} else{ //File extension null check
							if(strcmp(curr_file_dir.fext, "") == 0){ //Matching file extension (empty), so we found the file!
								file_dir = curr_file_dir;
								break;
							}
						}
					}
				}

				if(strcmp(file_dir.fname, "") != 0){ //Filename is empty, so don't continue
					if(offset > file_dir.fsize){ //Offset is bigger than the file size
						return -EFBIG;
					}
					
					//Find the starting block number and offset to read from
					int block_number_of_file = 0;
					if(offset != 0) block_number_of_file = offset/BLOCK_SIZE;
					int offset_of_block = 0;
					if(offset != 0) offset_of_block = offset - block_number_of_file*BLOCK_SIZE;

					//Iterate through the FAT to find the starting block
					int curr_block = file_dir.nStartBlock;
					if(block_number_of_file != 0){
						while(block_number_of_file > 0){
							curr_block = fat.table[curr_block];
							block_number_of_file--;
						}
					}

					//Open the disk to read the data from the respective blocks of the file
					FILE* disk = fopen(".disk", "r+b");
					fseek(disk, BLOCK_SIZE*curr_block+offset_of_block, SEEK_SET);
					cs1550_disk_block new_data;
					fread(&new_data.data, BLOCK_SIZE-offset_of_block, 1, disk);
					int curr_buffer_size = 0;

					//Append the read in data to the buffer
					if(file_dir.fsize >= BLOCK_SIZE){
						memcpy(buf, &new_data.data, BLOCK_SIZE-offset_of_block);
					} else{
						memcpy(buf, &new_data.data, file_dir.fsize);
					}

					curr_buffer_size = BLOCK_SIZE - offset_of_block; //Increase the size of the buffer

					//While this file hasn't ended yet and there are still more block sto read in, repeat the above procedure and keep iterating through the blocks of the file until EOF is reached
					while(fat.table[curr_block] != EOF){
						curr_block = fat.table[curr_block];

						cs1550_disk_block data;
						fseek(disk, BLOCK_SIZE*curr_block, SEEK_SET);
						fread(&data.data, BLOCK_SIZE, 1, disk);
						memcpy(buf+curr_buffer_size, &data, strlen(data.data));
						curr_buffer_size += strlen(data.data);
					}

					fclose(disk);

					//Write the root and FAT back to disk
					write_root(&root);
					write_fat(&fat);

					size = curr_buffer_size;
				} else{ //Filename is empty, so can't read from a directory
					return -EISDIR;
				}
			} else{ //Directory is empty, so can't read in anything
				return -EPERM;
			}
		} else{ //Directory is empty or null, so either don't read in anything or return that permission is denied
			if(strcmp(directory, "") == 0){
				return 0;
			} else if(strcmp(file_name, "") == 0){
				return -EPERM;
			}
		}	
	}

	return size; //Return the size of the buffer that is returned
}

/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
			  off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	printf("entering write\n");

	//path will be in the format of /directory/sub_directory
	char* directory; //The first directory in the 2-level file system
	char* file_name; //The directory within the root's directory	
	char* file_ext;

	//Parse the two strings
	int path_length = strlen(path);
	char path_copy[path_length];
	strcpy(path_copy, path);	

	directory = strtok(path_copy, "/");
	file_name = strtok(NULL, "."); //NULL indicates to continue where strtok left off at
	file_ext = strtok(NULL, ".");

	if((directory && directory[0]) && strcmp(directory, "") != 0){ //Directory and filename are not empty, so search for it
		if(file_name && file_name[0]){ //Null check for filename
			if(strcmp(file_name, "") == 0){ //Check if filename is empty
				return -EEXIST; //Can't read a file in the root directory
			}

			if(file_ext && file_ext[0]){ //Null check for file extension
				//CHeck if the filename or extension exceed their maximum sizes of the 8.3 format
				if(strlen(file_name) > MAX_FILENAME || strlen(file_ext) > MAX_EXTENSION){
					return -ENAMETOOLONG;
				}
			} else{
				//Check if the file extension exceeds 3 characters
				if(strlen(file_name) > MAX_FILENAME){
					return -ENAMETOOLONG;
				}
			}
		} else{ //The filename is null, so return that it doesn't exist
			return -EEXIST; //Can't read a file in the root directory
		}

		//Read in the root and FAT to get data later
		cs1550_root_directory root = read_root();
		cs1550_fat_block fat = read_fat();

		struct cs1550_directory dir;

		int i = 0;
		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){ //Iterate over the directories in the root
			struct cs1550_directory curr_dir = root.directories[i];
			if(strcmp(directory, curr_dir.dname) == 0){ //Found a matching directory!
				dir = curr_dir;
				break;
			}
		}

		if(strcmp(dir.dname, "") != 0){ //Valid directory was found
			long dir_location_on_disk = BLOCK_SIZE*dir.nStartBlock;

			//Open the disk, read in the directory entry we're looking for, and close the disk
			FILE* disk = fopen(".disk", "r+b");
			fseek(disk, dir_location_on_disk, SEEK_SET);	
			cs1550_directory_entry dir_entry;
			int success = fread(&dir_entry, BLOCK_SIZE, 1, disk);
			fclose(disk);
	
			if(success){ //The directory entry was successfully read in
				struct cs1550_file_directory file_dir;
				int file_directory_index = -1;

				int j = 0;
				for(j = 0; j < MAX_FILES_IN_DIR; j++){ //Search through the files in the directory to find the file we're trying to write to
					struct cs1550_file_directory curr_file_dir = dir_entry.files[j];

					if(strcmp(curr_file_dir.fname, file_name) == 0){ //Found a matching filename
						if(file_ext && file_ext[0]){ //Check whether the file we're looking for has a file extension or not; if true, it does
							if(strcmp(curr_file_dir.fext, file_ext) == 0){ //There is a matching file extension, so we found the file!
								file_dir = curr_file_dir;
								file_directory_index = j;
								break;
							}
						} else{ //File we're looking for doesn't have an extension
							if(strcmp(curr_file_dir.fext, "") == 0){ //Found the file!
								file_dir = curr_file_dir;
								file_directory_index = j;
								break;
							}
						}
					}
				}

				if(strcmp(file_dir.fname, "") != 0){ //Check if the filename is empty
					printf("file name: %s, file extension: %s, start block: %d\n", file_dir.fname, file_dir.fext, file_dir.nStartBlock);
					printf("offset: %d, file size: %d, buffer size: %d, size variable: %d", offset, file_dir.fsize, sizeof(buf)/sizeof(char*), size);
					printf(", strlen(buf): %d\n", strlen(buf));
					if(offset > file_dir.fsize){ //Offset is greater than the file size, so don't write
						return -EFBIG;
					}
					int buffer_size = strlen(buf); //Buffer size is the length of the buf
					int write_bytes_until_append = file_dir.fsize - offset; //The number of bytes we can write until we start appending
					
					//Calculate the number of blocks we must skip to get to a normal offset where we can just starting writing normally
					int block_number_of_file = 0; //Number of blocks that the offset must skip
					if(offset != 0) block_number_of_file = offset/BLOCK_SIZE;
					int offset_of_block = 0; //Once we reach the starting block, how much offset is left over?
					if(offset != 0) offset_of_block = offset - block_number_of_file*BLOCK_SIZE;

					//Iterate through the FAT until we get to the correct starting block that the offset belongs in
					int curr_block = file_dir.nStartBlock;
					if(block_number_of_file != 0){
						while(block_number_of_file > 0){
							curr_block = fat.table[curr_block];
							block_number_of_file--;
						}
					}

					int buffer_bytes_remaining = buffer_size; //The number of bytes remaining that we have to write to disk

					//Open the disk and write the correct about of buffer data in the first block; this basically gets rid of the offset so we can then start writing entire Blocks at a time later
					FILE* disk = fopen(".disk", "r+b");
					fseek(disk, BLOCK_SIZE*curr_block+offset_of_block, SEEK_SET);
					if(buffer_size >= BLOCK_SIZE){ //This means there will be left over stuff in the buffer that we have to write after we finish writing to this block
						fwrite(buf, BLOCK_SIZE-offset_of_block, 1, disk);
						buffer_bytes_remaining -= (BLOCK_SIZE-offset_of_block);

						if(offset == size){ //Expand the file size
							file_dir.fsize = offset+1;
						}
					} else{ //We can fit all of the data we want to write into this one block
						printf("buf: %p, buf[0]: %c, buffer_size: %d, size: %d\n", buf, buf[0], buffer_size, size);
						fwrite(buf, buffer_size, 1, disk);
						printf("buf: %s\n", buf);

						char null_array[BLOCK_SIZE-buffer_size];
						int m = 0;
						for(m = 0; m < BLOCK_SIZE-buffer_size; m++){
							null_array[m] = '\0';
						}
						fwrite(null_array, BLOCK_SIZE-buffer_size, 1, disk);
						buffer_bytes_remaining -= buffer_size;

						printf("offset: %d, file size: %d\n", offset, file_dir.fsize);
						if(offset == file_dir.fsize && !(offset == 0 && file_dir.fsize == 0)){
							printf("offset == file size, new file size: %d\n", file_dir.fsize);
						} else if(file_dir.fsize > size){ //File directory is greater than size, so we must re-adjust the file size to the length of size (e.g. "this is text" is replaced with "word");
							if(offset == size){
								printf("new size is being set because offset == size\n");
								file_dir.fsize = offset+1;
							} else{
								file_dir.fsize = size;
							}

							printf("AFTER SHORTENING, FILE SIZE IS: %d\n", file_dir.fsize);
						}
					}

					
					int bytes_to_clear = size - buffer_size;			

					while(buffer_bytes_remaining > 0){ //There's still more data to write from buf
						if(fat.table[curr_block] == EOF){ //Basically append data to a file; allocate more blocks in the FAT.
							int free_block_found = 0;
							int k = 2;
							for(k = 2; k < MAX_FAT_ENTRIES; k++){ //We need to allocate another block for this file to write more bytes; find a new block in the FAT
								if(fat.table[k] == 0){
									fat.table[curr_block] = k;
									fat.table[k] = EOF;
									curr_block = k;
									free_block_found = 1;
									break;
								}
							}

							if(!free_block_found){ //No more free blocks in the FAT.
								return -EPERM; //Can't write anymore to a file; ran out of memory on disk.
							}
						} else{ //We still have empty room on the file, so just continue writing into the next block
							curr_block = fat.table[curr_block];
						}

						fseek(disk, BLOCK_SIZE*curr_block, SEEK_SET);
						if(buffer_bytes_remaining >= BLOCK_SIZE){ //We'll still have to do more iterations after this because we have more data to write than one block
							char* new_buf_address = buf + (buffer_size - buffer_bytes_remaining);
							fwrite(new_buf_address, BLOCK_SIZE, 1, disk);
							buffer_bytes_remaining -= BLOCK_SIZE;
						} else{ //This is our final write because we don't have anything left in the buffer to write; so just write it and finish
							char* new_buf_address = buf + (buffer_size - buffer_bytes_remaining);
							fwrite(new_buf_address, buffer_bytes_remaining, 1, disk);

							buffer_bytes_remaining = 0; //buffer_bytes_remaining - buffer_bytes_remaining = 0 bytes to write
						}
					}
			
					//Calculate the number of bytes written and appended to the file
					int write_bytes = buffer_size - buffer_bytes_remaining; //The number of bytes written so far
					int appended_bytes = write_bytes - write_bytes_until_append;
					if(appended_bytes > 0){ //Increase the file size if there was an append
						file_dir.fsize += appended_bytes;
					}

					dir_entry.files[file_directory_index] = file_dir;
					fseek(disk, dir.nStartBlock*BLOCK_SIZE, SEEK_SET);
					fwrite(&dir_entry, BLOCK_SIZE, 1, disk);

					fclose(disk);

					write_root(&root);
					write_fat(&fat);

					size = buffer_size;

					printf("11 about to leve write, file size: %d, appended bytes: %d, write bytes: %d\n", file_dir.fsize, appended_bytes, write_bytes);
				} else{ //The directory entry failed to be read from disk; so return that this is a directyory and we can't write to it
					return -EISDIR;
				}
			} else{ //Directory name is empty; return there is no permissions
				return -EPERM;
			}
		} else{ //Directory name is empty or null, so return success since we don't have to write anything or that there are no permissions
			if(strcmp(directory, "") == 0){
				return 0;
			} else if(strcmp(file_name, "") == 0){
				return -EPERM;
			}
		}	
	}

	return size; //Return the amount of data that was written to file from the buffer
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
