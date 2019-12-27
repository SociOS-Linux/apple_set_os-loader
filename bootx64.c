#include <efi.h>
#include "include/int_graphics.h"
#include "include/int_print.h"
#include "include/int_event.h"
#include "include/int_guid.h"
#include "include/int_dpath.h"
#include "include/pci_db.h"


#define APPLE_SET_OS_VENDOR  "Apple Inc."
#define APPLE_SET_OS_VERSION "Mac OS X 10.15"

#define ACPI_ADDRESS_SPACE_TYPE_BUS   0x02
#define ACPI_END_TAG_DESCRIPTOR       0x79

#define CALC_EFI_PCI_ADDRESS(Bus, Dev, Func, Reg) \
    ((UINT64) ((((UINTN) Bus) << 24) + (((UINTN) Dev) << 16) + (((UINTN) Func) << 8) + ((UINTN) Reg)))

#pragma pack(1)
typedef struct {
  UINT8   Desc;
  UINT16  Len;
  UINT8   ResType;
  UINT8   GenFlag;
  UINT8   SpecificFlag;
  UINT64  AddrSpaceGranularity;
  UINT64  AddrRangeMin;
  UINT64  AddrRangeMax;
  UINT64  AddrTranslationOffset;
  UINT64  AddrLen;
} EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR;

typedef struct {
   UINT16  VendorId;
   UINT16  DeviceId;
   UINT16  Command;
   UINT16  Status;
   UINT8   RevisionId;
   UINT8   ClassCode[3];
   UINT8   CacheLineSize;
   UINT8   PrimaryLatencyTimer;
   UINT8   HeaderType;
   UINT8   Bist;
} PCI_COMMON_HEADER;
 
typedef struct {
   UINT32  Bar[6];               // Base Address Registers
   UINT32  CardBusCISPtr;        // CardBus CIS Pointer
   UINT16  SubVendorId;          // Subsystem Vendor ID
   UINT16  SubSystemId;          // Subsystem ID
   UINT32  ROMBar;               // Expansion ROM Base Address
   UINT8   CapabilitiesPtr;      // Capabilities Pointer
   UINT8   Reserved[3];
   UINT32  Reserved1;
   UINT8   InterruptLine;        // Interrupt Line
   UINT8   InterruptPin;         // Interrupt Pin
   UINT8   MinGnt;               // Min_Gnt
   UINT8   MaxLat;               // Max_Lat
} PCI_DEVICE_HEADER;
 
typedef struct {
   UINT32  CardBusSocketReg;     // Cardus Socket/ExCA Base Address Register
   UINT8   CapabilitiesPtr;      // 14h in pci-cardbus bridge.
   UINT8   Reserved;
   UINT16  SecondaryStatus;      // Secondary Status
   UINT8   PciBusNumber;         // PCI Bus Number
   UINT8   CardBusBusNumber;     // CardBus Bus Number
   UINT8   SubordinateBusNumber; // Subordinate Bus Number
   UINT8   CardBusLatencyTimer;  // CardBus Latency Timer
   UINT32  MemoryBase0;          // Memory Base Register 0
   UINT32  MemoryLimit0;         // Memory Limit Register 0
   UINT32  MemoryBase1;
   UINT32  MemoryLimit1;
   UINT32  IoBase0;
   UINT32  IoLimit0;             // I/O Base Register 0
   UINT32  IoBase1;              // I/O Limit Register 0
   UINT32  IoLimit1;
   UINT8   InterruptLine;        // Interrupt Line
   UINT8   InterruptPin;         // Interrupt Pin
   UINT16  BridgeControl;        // Bridge Control
} PCI_CARDBUS_HEADER;
 
typedef union {
   PCI_DEVICE_HEADER   Device;
   PCI_CARDBUS_HEADER  CardBus;
} NON_COMMON_UNION;
 
typedef struct {
   PCI_COMMON_HEADER Common;
   NON_COMMON_UNION  NonCommon;
   UINT32            Data[48];
} PCI_CONFIG_SPACE;
#pragma pack(0)


EFI_STATUS PciGetProtocolAndResource (
    IN  EFI_BOOT_SERVICES*                    BS,
    IN  EFI_HANDLE                            ImageHandle,
    IN  EFI_HANDLE                            Handle,
    OUT EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL**     IoDev,
    OUT EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR**   Descriptors
) {
    EFI_STATUS  Status;
    EFI_GUID efi_pci_root_bridge_io_guid = EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GUID;

    Status = BS->OpenProtocol (
        Handle,
        &efi_pci_root_bridge_io_guid,
        (VOID**)IoDev,
        ImageHandle,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );

    if (!EFI_ERROR (Status)) {
        
        Status = (*IoDev)->Configuration(*IoDev, (VOID**)Descriptors);
        if (Status == EFI_UNSUPPORTED) {
            *Descriptors = NULL;
        }
    }

    return Status;
}

EFI_STATUS PciGetNextBusRange (
    IN OUT EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  **Descriptors,
    OUT    UINT64                             *MinBus,
    OUT    UINT64                             *MaxBus,
    OUT    BOOLEAN                            *IsEnd
) {
    *IsEnd = FALSE;

    if ((*Descriptors) == NULL) {
        *MinBus = 0;
        *MaxBus = PCI_MAX_BUS;
        return EFI_SUCCESS;
    }

    while ((*Descriptors)->Desc != ACPI_END_TAG_DESCRIPTOR) {
        if ((*Descriptors)->ResType == ACPI_ADDRESS_SPACE_TYPE_BUS) {
        *MinBus = (*Descriptors)->AddrRangeMin;
        *MaxBus = (*Descriptors)->AddrRangeMax;
        (*Descriptors)++;
        return EFI_SUCCESS;
        }

        (*Descriptors)++;
    }

    if ((*Descriptors)->Desc == ACPI_END_TAG_DESCRIPTOR) {
        *IsEnd = TRUE;
    }

    return EFI_SUCCESS;
}


VOID PrintGpu(EFI_BOOT_SERVICES* BS, SIMPLE_TEXT_OUTPUT_INTERFACE* ConOut, EFI_HANDLE ImageHandle)
{
    EFI_STATUS Status;

    EFI_GUID efi_pci_enum_complete_guid = EFI_PCI_EMUMERATION_COMPLETE_GUID;
    EFI_GUID efi_pci_root_bridge_io_guid = EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GUID;
    VOID* pci_enum_interface;

    EFI_HANDLE* PciRootBridgeIoHandleBuf;
    UINTN PciRootBridgeIoHandleCount = 0;

    Status = gBS->LocateProtocol(
        &efi_pci_enum_complete_guid,
        NULL,
        &pci_enum_interface
    );

    if (EFI_ERROR(Status)) {
        _INT_IPrintAt(ConOut, 0, 10, L"PciEnumComplete Error: %lXs", (UINTN)Status);
    } else {
        Status = BS->LocateHandleBuffer(
            ByProtocol, 
            &efi_pci_root_bridge_io_guid, 
            NULL, 
            &PciRootBridgeIoHandleCount,
            &PciRootBridgeIoHandleBuf
        );


        if (EFI_ERROR(Status)) {
            _INT_IPrintAt(ConOut, 0, 10, L"PciRootBridgeIo Buffer Error: %lX", (UINTN)Status);
        } else if (PciRootBridgeIoHandleCount == 0) {
            _INT_IPrintAt(ConOut, 0, 10, L"No PciRootBridgeIo Handles");
        } else {
            _INT_IPrintAt(ConOut, 0, 10, L"PciRootBridgeIo handle Count: %d", PciRootBridgeIoHandleCount);

            EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL*    IoDev;
            EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR*  Descriptors;
            UINT64                              MinBus;
            UINT64                              MaxBus;
            BOOLEAN                             IsEnd;
            UINT64                              Address;
            PCI_CONFIG_SPACE                    ConfigSpace;
            //PCI_DEVICE_HEADER                   *DeviceHeader;
            PCI_COMMON_HEADER                   PciHeader;
            UINT16                              NumOfGpu;

            NumOfGpu = 0;

            for(UINTN i = 0; i < PciRootBridgeIoHandleCount; i++) {
                Status = PciGetProtocolAndResource(
                    BS,
                    ImageHandle,
                    PciRootBridgeIoHandleBuf[i],
                    &IoDev,
                    &Descriptors
                );

                if (EFI_ERROR(Status)) {
                    _INT_IPrintAt(ConOut, 0, 10, L"PciGetProtocolAndResource Error: %d", Status);
                    continue;
                }

                while (TRUE) {
                    Status = PciGetNextBusRange(&Descriptors, &MinBus, &MaxBus, &IsEnd);

                    if (EFI_ERROR(Status)) {
                        break;
                    }

                    if (IsEnd) {
                        break;
                    }

                    for (UINT64 Bus = MinBus; Bus <= MaxBus; Bus++) {
                        for (UINT16 Device = 0; Device <= PCI_MAX_DEVICE; Device++) {
                            for (UINT16 Func = 0; Func <= PCI_MAX_FUNC; Func++) {

                                Address = CALC_EFI_PCI_ADDRESS(Bus, Device, Func, 0);

                                Status = IoDev->Pci.Read(
                                    IoDev,
                                    EfiPciIoWidthUint8,
                                    Address,
                                    sizeof(ConfigSpace),
                                    &ConfigSpace
                                );
    
                                //DeviceHeader = (PCI_DEVICE_HEADER*)&(ConfigSpace.NonCommon.Device);
    
                                Status = IoDev->Pci.Read(
                                    IoDev,
                                    EfiPciIoWidthUint16,
                                    Address,
                                    1,
                                    &PciHeader.VendorId
                                );
    
                                if (PciHeader.VendorId == 0xffff && Func == 0) {
                                    break;
                                }
    
                                if (PciHeader.VendorId != 0xffff) {
                                    Status = IoDev->Pci.Read(
                                        IoDev,
                                        EfiPciIoWidthUint32,
                                        Address,
                                        sizeof(PciHeader) / sizeof(UINT32),
                                        &PciHeader
                                    );
        
                                    if (PciHeader.ClassCode[2] == 3) {
                                        CHAR16* VendorStr = L"Unknown";
                                        CHAR16* DeviceStr = L"Unknown";

                                        for (UINT16 vendorIdx = 0; vendorIdx < pci_vendor_db_size; vendorIdx++) {
                                            if (pci_vendor_db[vendorIdx]->vendorId == PciHeader.VendorId) {
                                                VendorStr = pci_vendor_db[vendorIdx]->vendorName;

                                                for (UINT16 deviceIdx = 0; deviceIdx < pci_vendor_db[vendorIdx]->numOfDevices; deviceIdx++) {
                                                    if (pci_vendor_db[vendorIdx]->devices[deviceIdx].deviceId == PciHeader.DeviceId) {
                                                        DeviceStr = pci_vendor_db[vendorIdx]->devices[deviceIdx].deviceName;
                                                    }
                                                }
                                                break;
                                            }
                                        }

                                        _INT_IPrintAt(ConOut, 0, 10 + NumOfGpu, L"%04x %04x %s - %s\t\t\t\t\t",
                                            PciHeader.VendorId, PciHeader.DeviceId, VendorStr, DeviceStr
                                        );

                                        NumOfGpu++;
                                    }

                                    if (Func == 0 &&
                                        ((PciHeader.HeaderType & HEADER_TYPE_MULTI_FUNCTION) == 0x00)) {
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            for (int clearIdx = 0; clearIdx < 3; clearIdx++) {
                _INT_IPrintAt(ConOut, 0, 10 + (NumOfGpu + clearIdx), L"\t\t\t\t\t\t");
            }
        }
    }

    _INT_FreePool(BS, PciRootBridgeIoHandleBuf);
}


EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
    EFI_STATUS Status;

    EFI_BOOT_SERVICES* BS = SystemTable->BootServices;
    SIMPLE_TEXT_OUTPUT_INTERFACE* ConOut = SystemTable->ConOut;
    SIMPLE_INPUT_INTERFACE* ConIn = SystemTable->ConIn;

    _INT_SetGraphicsMode(BS, FALSE);

    _INT_Clear(ConOut);
    _INT_IPrintAt(ConOut, 0, 0, L"============== apple_set_os loader v0.3 ==============");


    EFI_HANDLE* AppleSetOsHandleBuf;
    UINTN AppleSetOsHandleCount;

    _INT_IPrintAt(ConOut, 0, 1, L"Initializing SetOsProtocol\t\t\t\t\t");
    EFI_GUID apple_set_os_guid = APPLE_SET_OS_GUID;
    Status = BS->LocateHandleBuffer(
        ByProtocol, 
        &apple_set_os_guid, 
        NULL, 
        &AppleSetOsHandleCount,
        &AppleSetOsHandleBuf
    );
    
    if (EFI_ERROR(Status)) {
        _INT_IPrintAt(ConOut, 0, 1, L"SetOsProtocol Buffer Error: %lX\t\t\t\t\t", (UINTN)Status);
        goto restart;
    } else if (AppleSetOsHandleCount == 0) {
        _INT_IPrintAt(ConOut, 0, 1, L"No SetOsProtocol Handles\t\t\t\t\t");
        goto restart;
    } else {
        _INT_IPrintAt(ConOut, 0, 1, L"SetOsProtocol Handle Count: %d\t\t\t\t\t", (UINTN)AppleSetOsHandleCount);

        for(UINTN i = 0; i < AppleSetOsHandleCount; i++) {
            EFI_APPLE_SET_OS_IFACE* SetOsIface = NULL;

            Status = BS->OpenProtocol(
                AppleSetOsHandleBuf[i],
                &apple_set_os_guid,
                (VOID**)&SetOsIface,
                ImageHandle,
                NULL,
                EFI_OPEN_PROTOCOL_GET_PROTOCOL
            );

            if (EFI_ERROR(Status)) {
                _INT_IPrintAt(ConOut, 0, 1, L"SetOsProtocol Error: %d\t\t\t\t\t", (UINTN)Status);
            } else {
                if (SetOsIface->Version != 0){
                    _INT_IPrintAt(ConOut, 0, 1, L"Setting OsVendor\t\t\t\t\t");
                    Status = SetOsIface->SetOsVendor((CHAR8*) APPLE_SET_OS_VENDOR);
                    if (EFI_ERROR(Status)){
                        _INT_IPrintAt(ConOut, 0, 1, L"OsVendor Error: %lX\t\t\t\t\t", (UINTN)Status);
                    }

                    _INT_IPrintAt(ConOut, 0, 2, L"Setting OsVersion\t\t\t\t\t");
                    Status = SetOsIface->SetOsVersion((CHAR8*) APPLE_SET_OS_VERSION);
                    if (EFI_ERROR(Status)){
                        _INT_IPrintAt(ConOut, 0, 2, L"OsVersion Error: %lX\t\t\t\t\t", (UINTN)Status);
                    }
                }
            }
        }
    }

    _INT_FreePool(BS, AppleSetOsHandleBuf);

    /**/
    _INT_IPrintAt(ConOut, 0, 9, L"Connected Graphics Cards:\t\t\t\t\t");
    PrintGpu(BS, ConOut, ImageHandle);


    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
	EFI_DEVICE_PATH* DevicePath = NULL;
	EFI_HANDLE DriverHandle;

    _INT_IPrintAt(ConOut, 0, 3, L"Initializing LoadedImageProtocol...\t\t\t\t\t");
    EFI_GUID efi_loaded_image_protocol_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    Status = BS->HandleProtocol(ImageHandle, &efi_loaded_image_protocol_guid, (VOID**) &LoadedImage);
    if (EFI_ERROR(Status) || LoadedImage == NULL) {
        goto halt;
    }

    _INT_IPrintAt(ConOut, 0, 3, L"Locating bootx64_original.efi...\t\t\t\t\t");
    DevicePath = _INT_FileDevicePath(
        BS, 
        LoadedImage->DeviceHandle, 
        L"\\EFI\\Boot\\bootx64_original.efi"
    );

    if (DevicePath == NULL) {
        _INT_IPrintAt(ConOut, 0, 3, L"Unable to find bootx64_original.efi\t\t\t\t\t");
        goto halt;
	}


    _INT_IPrintAt(ConOut, 0, 3, L"Loading bootx64_original.efi to memory...\t\t\t\t\t");
    // Attempt to load the driver.
	Status = BS->LoadImage(FALSE, ImageHandle, DevicePath, NULL, 0, &DriverHandle);

    _INT_FreePool(BS, DevicePath);
    DevicePath = NULL;

	if (EFI_ERROR(Status)) {
        _INT_IPrintAt(ConOut, 0, 3, L"Unable to load bootx64_original.efi to memory\t\t\t\t\t");
		goto halt;
	}

    _INT_IPrintAt(ConOut, 0, 3, L"Prepare to run bootx64_original.efi...\t\t\t\t\t");

	Status = BS->OpenProtocol(
        DriverHandle, 
        &efi_loaded_image_protocol_guid,
		(VOID**)&LoadedImage, 
        ImageHandle, 
        NULL, 
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );
	if (EFI_ERROR(Status)) {
        _INT_IPrintAt(ConOut, 0, 3, L"Failed to run bootx64_original.efi\t\t\t\t\t");
		goto halt;
	}

/////
    _INT_IPrintAt(ConOut, 0, 3, L"\t\t\t\t\t\t");
    _INT_IPrintAt(ConOut, 0, 4, L"------------------- Ready to boot --------------------");
    _INT_IPrintAt(ConOut, 0, 5, L"-------- Plug in your eGPU then press any key --------");

    EFI_INPUT_KEY Key;

    for (UINT8 i = 6; i > 0; i--) {
        _INT_IPrintAt(ConOut, 0, 6, L"Boot bootx64_original.efi in %u second(s)\t\t\t\t\t", (UINT32)i);

        if ((i < 5) && (i % 2 == 0)) {
            PrintGpu(BS, ConOut, ImageHandle);
            _INT_WaitForSingleEvent(BS, ConIn->WaitForKey, 10000);
            if (!EFI_ERROR(ConIn->ReadKeyStroke(ConIn, &Key))) {
                goto run;
            }
        } else {
            for (UINT16 j = 0; j < 1000; j++) {
                _INT_WaitForSingleEvent(BS, ConIn->WaitForKey, 10000);
                if (!EFI_ERROR(ConIn->ReadKeyStroke(ConIn, &Key))) {
                    goto run;
                }
            }
        }
    }

run:
    _INT_IPrintAt(ConOut, 0, 6, L"Starting bootx64_original.efi...\t\t\t\t\t");
    for (UINT16 j = 0; j < 1000; j++) {
        _INT_WaitForSingleEvent(BS, ConIn->WaitForKey, 10000);
    }
    ConOut->ClearScreen(ConOut);
    _INT_SetGraphicsMode(BS, TRUE);

    // Load was a success - attempt to start the driver
    Status = BS->StartImage(DriverHandle, NULL, NULL);
    if (EFI_ERROR(Status)) {
        _INT_IPrintAt(ConOut, 0, 6, L"Unable to start bootx64_original.efi\t\t\t\t\t");
        goto halt;
    }

halt:
    while (1) { BS->Stall(100000); }
    return EFI_SUCCESS;

restart:
        _INT_IPrintAt(ConOut, 0, 4, L"SetOsProtocol is not loaded, restarting...\t\t\t\t\t");
        for (UINT16 j = 0; j < 250; j++) {
            _INT_WaitForSingleEvent(BS, ConIn->WaitForKey, 10000);
        }
        return SystemTable->RuntimeServices->ResetSystem(
            EfiResetWarm,
            EFI_SUCCESS,
            sizeof(NULL),
            NULL
        );
}
