#pragma once
#include "typedefs.h"

struct VNode {
	char Name[MAX_NAME_SIZE] = { 0 };

	filesystem_t FSDescriptor;
	inode_t Inode;
}__attribute__((packed));