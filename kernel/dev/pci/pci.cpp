#include <dev/pci/pci.hpp>
#include <mm/vmm.hpp>
#include <sys/printk.hpp>
#include <mm/memory.hpp>

static uint64_t hhdm = hhdmRequest.response->offset;

namespace PCI {
void EnumeratePCI(ACPI::MCFGHeader *mcfg) {
	int entries = ((mcfg->Header.Length) - sizeof(ACPI::MCFGHeader)) / sizeof(ACPI::DeviceConfig);

	PRINTK::PrintK("Enumerating the PCI bus...\r\n");
	for (int i = 0; i < entries; i++) {
		ACPI::DeviceConfig *newDeviceConfig = (ACPI::DeviceConfig*)((uint64_t)mcfg + sizeof(ACPI::MCFGHeader) + (sizeof(ACPI::DeviceConfig) * i));

		for (uint64_t bus = newDeviceConfig->StartBus; bus < newDeviceConfig->EndBus; bus++) {
			PCIBus *newBus = new PCIBus(newDeviceConfig->BaseAddress, bus);

			if(!newBus->Exists()) {
				delete newBus;
			} else {
				newBus->SetMajor(1);
				newBus->SetMinor(0);

				AddDevice(newBus);
			}
		}
	}
}

PCIBus::PCIBus(uint64_t baseAddress, uint64_t bus) {
	this->baseAddress = baseAddress;
	this->bus = bus;

	uint64_t offset = bus << 20;
	busAddress = baseAddress + offset;

	//VMM::MapMemory((void*)busAddress + hhdm, (void*)busAddress);

	PCIDeviceHeader *pciDeviceHeader = (PCIDeviceHeader*)busAddress;

	if(pciDeviceHeader->DeviceID == 0) {
		exists = false;
		return;
	}

	if(pciDeviceHeader->DeviceID == 0xFFFF) {
		exists = false;
		return;
	}

	for (uint64_t device = 0; device < 32; device++) {
		devices[device] = new PCIDevice(busAddress, device);

		if (!devices[device]->Exists()) {
			delete devices[device];
			devices[device] = NULL;
		} else {
			devices[device]->SetMajor(1);
			devices[device]->SetMinor(1);
		}
	}
}

PCIDevice::PCIDevice(uint64_t busAddress, uint64_t device) {
	this->busAddress = busAddress;
	this->device = device;

	uint64_t offset = device << 15;

	uint64_t deviceAddress = busAddress + offset;
	//VMM::MapMemory((void*)deviceAddress + hhdm, (void*)deviceAddress);

	PCIDeviceHeader *pciDeviceHeader = (PCIDeviceHeader*)deviceAddress;

	if(pciDeviceHeader->DeviceID == 0) {
		exists = false;
		return;
	}

	if(pciDeviceHeader->DeviceID == 0xFFFF) {
		exists = false;
		return;
	}

	for (uint64_t function = 0; function < 8; function++) {
		functions[function] = new PCIFunction(deviceAddress, function);

		if (!functions[function]->Exists()) {
			delete functions[function];
			functions[function] = NULL;
		}
	}
}

PCIFunction::PCIFunction(uint64_t deviceAddress, uint64_t function) {
	this->deviceAddress = deviceAddress;
	this->function = function;

	uint64_t offset = function<< 12;

	functionAddress = deviceAddress + offset;
	//VMM::MapMemory((void*)functionAddress + hhdm, (void*)functionAddress);

	PCIDeviceHeader *pciDeviceHeader = (PCIDeviceHeader*)functionAddress;

	if(pciDeviceHeader->DeviceID == 0) {
		exists = false;
		return;
	}

	if(pciDeviceHeader->DeviceID == 0xFFFF) {
		exists = false;
		return;
	}

	PRINTK::PrintK("PCI device: 0x%x - 0x%x - 0x%x - 0x%x\n",
			pciDeviceHeader->VendorID,
			pciDeviceHeader->DeviceID,
			pciDeviceHeader->Subclass,
			pciDeviceHeader->ProgIF);

	return;
}
}