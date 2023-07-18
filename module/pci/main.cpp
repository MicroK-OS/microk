#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include <cdefs.h>
#include <mkmi.h>
#include <mkmi_log.h>
#include <mkmi_memory.h>
#include <mkmi_syscall.h>

extern "C" uint32_t VendorID = 0xCAFEBABE;
extern "C" uint32_t ProductID = 0xB830C0DE;

const char* DeviceClasses[] {
	"Unclassified",
		"Mass Storage Controller",
		"Network Controller",
		"Display Controller",
		"Multimedia Controller",
		"Memory Controller",
		"Bridge Device",
		"Simple Communication Controller",
		"Base System Peripheral",
		"Input Device Controller",
		"Docking Station", 
		"Processor",
		"Serial Bus Controller",
		"Wireless Controller",
		"Intelligent Controller",
		"Satellite Communication Controller",
		"Encryption Controller",
		"Signal Processing Controller",
		"Processing Accelerator",
		"Non Essential Instrumentation"
};

const char* GetVendorName(uint16_t VendorID){
	switch (VendorID){
		case 0x8086:
			return "Intel Corp";
		case 0x1022:
			return "AMD";
		case 0x10DE:
			return "NVIDIA Corporation";
	}
	return "Unknown";
}

const char* GetDeviceName(uint16_t VendorID, uint16_t DeviceID){
	switch (VendorID){
		case 0x8086: // Intel
			switch(DeviceID){
				case 0x29C0:
					return "Express DRAM Controller";
				case 0x2918:
					return "LPC Interface Controller";
				case 0x2922:
					return "6 port SATA Controller [AHCI mode]";
				case 0x2930:
					return "SMBus Controller";
			}
	}
	return "Unknown";
}

const char* MassStorageControllerSubclassName(uint8_t SubclassCode){
	switch (SubclassCode){
		case 0x00:
			return "SCSI Bus Controller";
		case 0x01:
			return "IDE Controller";
		case 0x02:
			return "Floppy Disk Controller";
		case 0x03:
			return "IPI Bus Controller";
		case 0x04:
			return "RAID Controller";
		case 0x05:
			return "ATA Controller";
		case 0x06:
			return "Serial ATA";
		case 0x07:
			return "Serial Attached SCSI";
		case 0x08:
			return "Non-Volatile Memory Controller";
		case 0x80:
			return "Other";
	}
	return "Unknown";
}

const char* SerialBusControllerSubclassName(uint8_t SubclassCode){
	switch (SubclassCode){
		case 0x00:
			return "FireWire (IEEE 1394) Controller";
		case 0x01:
			return "ACCESS Bus";
		case 0x02:
			return "SSA";
		case 0x03:
			return "USB Controller";
		case 0x04:
			return "Fibre Channel";
		case 0x05:
			return "SMBus";
		case 0x06:
			return "Infiniband";
		case 0x07:
			return "IPMI Interface";
		case 0x08:
			return "SERCOS Interface (IEC 61491)";
		case 0x09:
			return "CANbus";
		case 0x80:
			return "SerialBusController - Other";
	}
	return "Unknown";
}

const char* BridgeDeviceSubclassName(uint8_t SubclassCode){
	switch (SubclassCode){
		case 0x00:
			return "Host Bridge";
		case 0x01:
			return "ISA Bridge";
		case 0x02:
			return "EISA Bridge";
		case 0x03:
			return "MCA Bridge";
		case 0x04:
			return "PCI-to-PCI Bridge";
		case 0x05:
			return "PCMCIA Bridge";
		case 0x06:
			return "NuBus Bridge";
		case 0x07:
			return "CardBus Bridge";
		case 0x08:
			return "RACEway Bridge";
		case 0x09:
			return "PCI-to-PCI Bridge";
		case 0x0a:
			return "InfiniBand-to-PCI Host Bridge";
		case 0x80:
			return "Other";
	}
	return "Unknown";
}

const char* GetSubclassName(uint8_t ClassCode, uint8_t SubclassCode){
	switch (ClassCode){
		case 0x01:
			return MassStorageControllerSubclassName(SubclassCode);
		case 0x03:
			switch (SubclassCode){
				case 0x00:
					return "VGA Compatible Controller";
			}
		case 0x06:
			return BridgeDeviceSubclassName(SubclassCode);
		case 0x0C:
			return SerialBusControllerSubclassName(SubclassCode);
	}
	return "Unknown";
}

const char* GetProgIFName(uint8_t ClassCode, uint8_t SubclassCode, uint8_t ProgIF){
	switch (ClassCode){
		case 0x01:
			switch (SubclassCode){
				case 0x06:
					switch (ProgIF){
						case 0x00:
							return "Vendor Specific Interface";
						case 0x01:
							return "AHCI 1.0";
						case 0x02:
							return "Serial Storage Bus";
					}
			}
		case 0x03:
			switch (SubclassCode){
				case 0x00:
					switch (ProgIF){
						case 0x00:
							return "VGA Controller";
						case 0x01:
							return "8514-Compatible Controller";
					}
			}
		case 0x0C:
			switch (SubclassCode){
				case 0x03:
					switch (ProgIF){
						case 0x00:
							return "UHCI Controller";
						case 0x10:
							return "OHCI Controller";
						case 0x20:
							return "EHCI (USB2) Controller";
						case 0x30:
							return "XHCI (USB3) Controller";
						case 0x80:
							return "Unspecified";
						case 0xFE:
							return "USB Device (Not a Host Controller)";
					}
			}    
	}
	return "Unknown";
}


#include "pci.h"
void EnumerateFunction(uint64_t deviceAddress, uint64_t function){
	uint64_t offset = function << 12;

	uint64_t functionAddress = deviceAddress + offset;
	Syscall(SYSCALL_MEMORY_MMAP, functionAddress, functionAddress, 4096, 0, 0, 0);

	PCIDeviceHeader* pciDeviceHeader = (PCIDeviceHeader*)functionAddress;

	if (pciDeviceHeader->DeviceID == 0) return;
	if (pciDeviceHeader->DeviceID == 0xFFFF) return;

	MKMI_Printf(" +---- PCI Device:\r\n"
		    " |     |- Vendor: %s (%x)\r\n"
		    " |     |- Device: %s (%x)\r\n"
		    " |     |- Class: %s\r\n"
		    " |     |- Subclass: %s (%x)\r\n"
		    " |     \\- Prog IF: %s (%x)\r\n"
		    " |\r\n",
		    GetVendorName(pciDeviceHeader->VendorID),
		    pciDeviceHeader->VendorID,
		    GetDeviceName(pciDeviceHeader->VendorID, pciDeviceHeader->DeviceID),
		    pciDeviceHeader->DeviceID,
		    DeviceClasses[pciDeviceHeader->Class],
		    GetSubclassName(pciDeviceHeader->Class, pciDeviceHeader->Subclass),
		    pciDeviceHeader->Subclass,
		    GetProgIFName(pciDeviceHeader->Class, pciDeviceHeader->Subclass, pciDeviceHeader->ProgIF),
		    pciDeviceHeader->ProgIF);
}

void EnumerateDevice(uint64_t busAddress, uint64_t device){
	uint64_t offset = device << 15;

	uint64_t deviceAddress = busAddress + offset;
	Syscall(SYSCALL_MEMORY_MMAP, deviceAddress, deviceAddress, 4096, 0, 0, 0);

	PCIDeviceHeader* pciDeviceHeader = (PCIDeviceHeader*)deviceAddress;

	if (pciDeviceHeader->DeviceID == 0) return;
	if (pciDeviceHeader->DeviceID == 0xFFFF) return;

	for (uint64_t function = 0; function < 8; function++){
		EnumerateFunction(deviceAddress, function);
	}
}

void EnumerateBus(uint64_t baseAddress, uint64_t bus){
	uint64_t offset = bus << 20;

	uint64_t busAddress = baseAddress + offset;
	Syscall(SYSCALL_MEMORY_MMAP, busAddress, busAddress, 4096, 0, 0, 0);

	PCIDeviceHeader* pciDeviceHeader = (PCIDeviceHeader*)busAddress;

	if (pciDeviceHeader->DeviceID == 0) return;
	if (pciDeviceHeader->DeviceID == 0xFFFF) return;

	for (uint64_t device = 0; device < 32; device++){
		EnumerateDevice(busAddress, device);
	}
    }


void EnumeratePCI(MCFGHeader *mcfg) {
	int entries = ((mcfg->Header.Length) - sizeof(MCFGHeader)) / sizeof(DeviceConfig);

	MKMI_Printf("Enumerating the PCI bus...\r\n");
	for (int i = 0; i < entries; i++) {
		/* Get the PCI device config */
		DeviceConfig *newDeviceConfig = (DeviceConfig*)((uint64_t)mcfg + sizeof(MCFGHeader) + (sizeof(DeviceConfig) * i));
		for (uint64_t bus = newDeviceConfig->StartBus;
				bus < newDeviceConfig->EndBus;
				bus++) {
			EnumerateBus(newDeviceConfig->BaseAddress, bus);
		}
	}
}
struct Message {
	uint32_t SenderVendorID : 32;
	uint32_t SenderProductID : 32;

	size_t MessageSize : 64;
}__attribute__((packed));

#include "../user/vfs/fops.h"

static bool ichi = true;
extern "C" size_t OnMessage() {
	uintptr_t bufAddr = 0xD000000000;

	Message *msg = bufAddr;
	uint32_t *signature = bufAddr + 128;

	MKMI_Printf("Message:\r\n"
		    " - Sender: %x by %x\r\n"
		    " - Size: %d\r\n"
		    " - Result: %x\r\n",
		    msg->SenderProductID, msg->SenderVendorID,
		    msg->MessageSize, *signature);

	if (ichi) {
		if (*signature != FILE_RESPONSE_MAGIC_NUMBER) return 0;

		FileOperationRequest *request = bufAddr + 128;

		inode_t dev = request->Data.Inode;

		request->MagicNumber = FILE_REQUEST_MAGIC_NUMBER;
		request->Request = NODE_CREATE;
		request->Data.Directory = dev;
		request->Data.Properties = NODE_PROPERTY_DIRECTORY;
		Memcpy(request->Data.Name, "pci", 3);

		Syscall(SYSCALL_MODULE_MESSAGE_SEND, 0xCAFEBABE, 0xDEADBEEF, 1, 0, 1, 1024);
		ichi = false;
	}

	return 0;
}

extern "C" size_t OnSignal() {
	MKMI_Printf("Signal!\r\n");
	return 0;
}

extern "C" size_t OnInit() {
	void *mcfg;
	size_t mcfgSize;
	Syscall(SYSCALL_FILE_OPEN, "ACPI:MCFG", &mcfg, &mcfgSize, 0, 0, 0);

	/* Here we check whether it exists 
	 * If it isn't there, just skip this step
	 */
	if (mcfg != 0 && mcfgSize != 0) {
		/* Make it accessible in memory */
		Syscall(SYSCALL_MEMORY_MMAP, mcfg, mcfg, mcfgSize, 0, 0, 0);

		MKMI_Printf("MCFG at 0x%x, size: %d\r\n", mcfg, mcfgSize);

		Syscall(SYSCALL_MODULE_BUS_REGISTER, "PCI", VendorID, ProductID, 0, 0 ,0);
		Syscall(SYSCALL_MODULE_BUS_REGISTER, "PCIe", VendorID, ProductID, 0, 0 ,0);

		uint32_t pid, vid;
		Syscall(SYSCALL_MODULE_BUS_GET, "PCI", &pid, &vid, 0, 0 ,0);
		MKMI_Printf("Cross check -> VID: %x PID: %x\r\n", vid, pid);

		EnumeratePCI(mcfg);
	} else {
		MKMI_Printf("No MCFG found.\r\n");
		return 1;
	}

	uintptr_t bufAddr = 0xD000000000;
	size_t bufSize = 4096 * 2;
	uint32_t bufID;
	bufID = Syscall(SYSCALL_MODULE_BUFFER_REGISTER, bufAddr, bufSize, 0x02, 0, 0, 0);

	FileOperationRequest *request = bufAddr + 128;
	request->MagicNumber = FILE_REQUEST_MAGIC_NUMBER;
	request->Request = NODE_FINDINDIR;
	request->Data.Directory = 0;
	request->Data.Properties = NODE_PROPERTY_DIRECTORY;
	Memcpy(request->Data.Name, "dev", 3);

	Syscall(SYSCALL_MODULE_MESSAGE_SEND, 0xCAFEBABE, 0xDEADBEEF, 1, 0, 1, 1024);

	return 0;
}

extern "C" size_t OnExit() {

	return 0;
}
