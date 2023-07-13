#include "vfs.h"
#include "fops.h"
#include "typedefs.h"

#include <mkmi_memory.h>
#include <mkmi_log.h>

void HelloWorld() {
	MKMI_Printf("We have been called to do shit.\r\n");
}

VirtualFilesystem::VirtualFilesystem() {
	BaseNode = new RegisteredFilesystemNode;
	BaseNode->FS = NULL;
	BaseNode->Next = NULL;
}


VirtualFilesystem::~VirtualFilesystem() {
	delete BaseNode;
}
	
filesystem_t VirtualFilesystem::RegisterFilesystem(uint32_t vendorID, uint32_t productID, void *instance, NodeOperations *ops) {
	/* It's us */
	Filesystem *fs = new Filesystem;

	fs->FSDescriptor = GetFSDescriptor();
	fs->OwnerVendorID = vendorID;
	fs->OwnerProductID = productID;
	fs->Instance = instance;
	fs->Operations = ops;

	RegisteredFilesystemNode *node = AddNode(fs);

	MKMI_Printf("Registered filesystem (ID: %x, VID: %x, PID: %x)\r\n",
			node->FS->FSDescriptor,
			node->FS->OwnerVendorID,
			node->FS->OwnerProductID);

	return fs->FSDescriptor;
}
	
#define IF_IS_OURS( x ) \
	if (node->FS->OwnerVendorID == 0 && node->FS->OwnerProductID == 0) return x



uintmax_t VirtualFilesystem::DoFilesystemOperation(filesystem_t fs, FileOperationRequest *request) {
	bool found = false;
	RegisteredFilesystemNode *previous; 
	RegisteredFilesystemNode *node = FindNode(fs, &previous, &found);

	if (node == NULL) return 0;
	if (request == NULL) return 0;


	switch(request->Request) {
		case NODE_CREATE:
			IF_IS_OURS(node->FS->Operations->CreateNode(node->FS->Instance, request->Data.Name));
			break;
		case NODE_GET:
			IF_IS_OURS(node->FS->Operations->GetNode(node->FS->Instance, request->Data.Inode));
			break;
		default:
			return 0;
	}

	return 0;
}

RegisteredFilesystemNode *VirtualFilesystem::AddNode(Filesystem *fs) {
	bool found = false;
	RegisteredFilesystemNode *node, *prev;
	node = FindNode(fs->FSDescriptor, &prev, &found);

	/* Already present, return this one */
	if(found) return node;

	/* If not, prev is now our last node. */
	RegisteredFilesystemNode *newNode = new RegisteredFilesystemNode;
	newNode->FS = fs;
	newNode->Next = NULL;

	prev->Next = newNode;
	
	return prev->Next;
}

void VirtualFilesystem::RemoveNode(filesystem_t fs) {
	bool found = false;
	RegisteredFilesystemNode *previous; 
	RegisteredFilesystemNode *node = FindNode(fs, &previous, &found);

	/* This issue shall be reported */
	if(!found) return;

	previous->Next = node->Next;

	delete node->FS;
	delete node;
}

RegisteredFilesystemNode *VirtualFilesystem::FindNode(filesystem_t fs, RegisteredFilesystemNode **previous, bool *found) {
	RegisteredFilesystemNode *node = BaseNode->Next, *prev = BaseNode;

	if (node == NULL) {
		*previous = prev;
		*found = false;
		return NULL;
	}


	while(true) {
		if (node->FS->FSDescriptor == fs) {
			*found = true;
			*previous = prev;
			return node;
		}

		prev = node;
		if (node->Next == NULL) break;
		node = node->Next;
	}

	*previous = prev;
	*found = false;
	return NULL;
}
