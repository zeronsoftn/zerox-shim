/** @file
  BDS Lib functions which relate with connect the device

Copyright (c) 2004 - 2008, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "shim.h"

#define MAX_UINT32   ((UINT32)0xFFFFFFFF)

#define MAX_DEVICE_PATH_NODE_COUNT   1024

#define EFI_HANDLE_TYPE_UNKNOWN                     0x000
#define EFI_HANDLE_TYPE_IMAGE_HANDLE                0x001
#define EFI_HANDLE_TYPE_DRIVER_BINDING_HANDLE       0x002
#define EFI_HANDLE_TYPE_DEVICE_DRIVER               0x004
#define EFI_HANDLE_TYPE_BUS_DRIVER                  0x008
#define EFI_HANDLE_TYPE_DRIVER_CONFIGURATION_HANDLE 0x010
#define EFI_HANDLE_TYPE_DRIVER_DIAGNOSTICS_HANDLE   0x020
#define EFI_HANDLE_TYPE_COMPONENT_NAME_HANDLE       0x040
#define EFI_HANDLE_TYPE_DEVICE_HANDLE               0x080
#define EFI_HANDLE_TYPE_PARENT_HANDLE               0x100
#define EFI_HANDLE_TYPE_CONTROLLER_HANDLE           0x200
#define EFI_HANDLE_TYPE_CHILD_HANDLE                0x400

//region PCI
#define PCI_CLASS_DISPLAY          0x03
#define   PCI_CLASS_DISPLAY_VGA    0x00
#define     PCI_IF_VGA_VGA         0x00

#define PCI_CLASS_SERIAL                 0x0C
#define   PCI_CLASS_SERIAL_USB           0x03

/**
  Macro that checks whether the Base Class code of device matched.

@param  _p      Specified device.
		@param  c       Base Class code needs matching.

		@retval TRUE    Base Class code matches the specified device.
		@retval FALSE   Base Class code doesn't match the specified device.

				**/
#define IS_CLASS1(_p, c)  ((_p)->Hdr.ClassCode[2] == (c))

/**
  Macro that checks whether the Base Class code and Sub-Class code of device matched.

  @param  _p      Specified device.
  @param  c       Base Class code needs matching.
  @param  s       Sub-Class code needs matching.

  @retval TRUE    Base Class code and Sub-Class code match the specified device.
  @retval FALSE   Base Class code and Sub-Class code don't match the specified device.

**/
#define IS_CLASS2(_p, c, s)  (IS_CLASS1 (_p, c) && ((_p)->Hdr.ClassCode[1] == (s)))

/**
  Macro that checks whether the Base Class code, Sub-Class code and Interface code of device matched.

  @param  _p      Specified device.
  @param  c       Base Class code needs matching.
  @param  s       Sub-Class code needs matching.
  @param  p       Interface code needs matching.

  @retval TRUE    Base Class code, Sub-Class code and Interface code match the specified device.
  @retval FALSE   Base Class code, Sub-Class code and Interface code don't match the specified device.

**/
#define IS_CLASS3(_p, c, s, p)  (IS_CLASS2 (_p, c, s) && ((_p)->Hdr.ClassCode[0] == (p)))

/**
  Macro that checks whether device is a VGA-compatible controller.

  @param  _p      Specified device.

  @retval TRUE    Device is a VGA-compatible controller.
  @retval FALSE   Device is not a VGA-compatible controller.

**/
#define IS_PCI_VGA(_p)  IS_CLASS3 (_p, PCI_CLASS_DISPLAY, PCI_CLASS_DISPLAY_VGA, PCI_IF_VGA_VGA)
//endregion

/**
  Determine whether a given device path is valid.

  @param  DevicePath  A pointer to a device path data structure.
  @param  MaxSize     The maximum size of the device path data structure.

  @retval TRUE        DevicePath is valid.
  @retval FALSE       DevicePath is NULL.
  @retval FALSE       Maxsize is less than sizeof(EFI_DEVICE_PATH_PROTOCOL).
  @retval FALSE       The length of any node in the DevicePath is less
                      than sizeof (EFI_DEVICE_PATH_PROTOCOL).
  @retval FALSE       If MaxSize is not zero, the size of the DevicePath
                      exceeds MaxSize.
  @retval FALSE       If PcdMaximumDevicePathNodeCount is not zero, the node
                      count of the DevicePath exceeds PcdMaximumDevicePathNodeCount.
**/
BOOLEAN
IsDevicePathValid (
		CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath,
		UINTN                    MaxSize
)
{
	UINTN Count;
	UINTN Size;
	UINTN NodeLength;

	//
	//  Validate the input whether exists and its size big enough to touch the first node
	//
	if (DevicePath == NULL || (MaxSize > 0 && MaxSize < END_DEVICE_PATH_LENGTH)) {
		return FALSE;
	}

	if (MaxSize == 0) {
		MaxSize = MAX_UINT32;
	}

	for (Count = 0, Size = 0; !IsDevicePathEnd (DevicePath); DevicePath = NextDevicePathNode (DevicePath)) {
		NodeLength = DevicePathNodeLength (DevicePath);
		if (NodeLength < sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
			return FALSE;
		}

		if (NodeLength > MAX_UINT32 - Size) {
			return FALSE;
		}
		Size += NodeLength;

		//
		// Validate next node before touch it.
		//
		if (Size > MaxSize - END_DEVICE_PATH_LENGTH ) {
			return FALSE;
		}

		Count++;
		if (Count >= MAX_DEVICE_PATH_NODE_COUNT) {
			return FALSE;
		}

	}

	//
	// Only return TRUE when the End Device Path node is valid.
	//
	return (BOOLEAN) (DevicePathNodeLength (DevicePath) == END_DEVICE_PATH_LENGTH);
}

#if 0

/**
  Creates a copy of the current device path instance and returns a pointer to the next device path
  instance.

	This function creates a copy of the current device path instance. It also updates
  DevicePath to point to the next device path instance in the device path (or NULL
  if no more) and updates Size to hold the size of the device path instance copy.
  If DevicePath is NULL, then NULL is returned.
  If DevicePath points to a invalid device path, then NULL is returned.
  If there is not enough memory to allocate space for the new device path, then
  NULL is returned.
  The memory is allocated from EFI boot services memory. It is the responsibility
  of the caller to free the memory allocated.
  If Size is NULL, then ASSERT().

  @param  DevicePath                 On input, this holds the pointer to the current
                                     device path instance. On output, this holds
                                     the pointer to the next device path instance
                                     or NULL if there are no more device path
                                     instances in the device path pointer to a
                                     device path data structure.
  @param  Size                       On output, this holds the size of the device
                                     path instance, in bytes or zero, if DevicePath
                                     is NULL.

  @return A pointer to the current device path instance.

**/
static EFI_DEVICE_PATH_PROTOCOL *
GetNextDevicePathInstance (
		EFI_DEVICE_PATH_PROTOCOL    **DevicePath,
		UINTN                          *Size
)
{
	EFI_DEVICE_PATH_PROTOCOL  *DevPath;
	EFI_DEVICE_PATH_PROTOCOL  *ReturnValue;
	UINT8                     Temp;

	ASSERT (Size != NULL);

	if (DevicePath == NULL || *DevicePath == NULL) {
		*Size = 0;
		return NULL;
	}

	if (!IsDevicePathValid (*DevicePath, 0)) {
		return NULL;
	}

	//
	// Find the end of the device path instance
	//
	DevPath = *DevicePath;
	while (!IsDevicePathEndType (DevPath)) {
		DevPath = NextDevicePathNode (DevPath);
	}

	//
	// Compute the size of the device path instance
	//
	*Size = ((UINTN) DevPath - (UINTN) (*DevicePath)) + sizeof (EFI_DEVICE_PATH_PROTOCOL);

	//
	// Make a copy and return the device path instance
	//
	Temp              = DevPath->SubType;
	DevPath->SubType  = END_ENTIRE_DEVICE_PATH_SUBTYPE;
	ReturnValue       = DuplicateDevicePath (*DevicePath);
	DevPath->SubType  = Temp;

	//
	// If DevPath is the end of an entire device path, then another instance
	// does not follow, so *DevicePath is set to NULL.
	//
	if (DevicePathSubType (DevPath) == END_ENTIRE_DEVICE_PATH_SUBTYPE) {
		*DevicePath = NULL;
	} else {
		*DevicePath = NextDevicePathNode (DevPath);
	}

	return ReturnValue;
}

#endif

/**
  This function will connect all current system handles recursively.

  gBS->ConnectController() service is invoked for each handle exist in system handler buffer.
  If the handle is bus type handler, all childrens also will be connected recursively
  by gBS->ConnectController().

  @retval EFI_SUCCESS           All handles and it's child handle have been connected
  @retval EFI_STATUS            Error status returned by of gBS->LocateHandleBuffer().

**/
EFI_STATUS
EFIAPI
BdsLibConnectAllEfi(
		void
) {
	EFI_STATUS Status;
	UINTN HandleCount;
	EFI_HANDLE *HandleBuffer;
	UINTN Index;

	Status = gBS->LocateHandleBuffer(
			AllHandles,
			NULL,
			NULL,
			&HandleCount,
			&HandleBuffer
	);
	if (EFI_ERROR(Status)) {
		return Status;
	}

	for (Index = 0; Index < HandleCount; Index++) {
		//Status =
		gBS->ConnectController(HandleBuffer[Index], NULL, NULL, true);
	}

	if (HandleBuffer != NULL) {
		FreePool(HandleBuffer);
	}

	return EFI_SUCCESS;
}

/**
  This function will disconnect all current system handles.

  gBS->DisconnectController() is invoked for each handle exists in system handle buffer.
  If handle is a bus type handle, all childrens also are disconnected recursively by
  gBS->DisconnectController().

  @retval EFI_SUCCESS           All handles have been disconnected
  @retval EFI_STATUS            Error status returned by of gBS->LocateHandleBuffer().

**/
EFI_STATUS
EFIAPI
BdsLibDisconnectAllEfi(
		void
) {
	EFI_STATUS Status;
	UINTN HandleCount;
	EFI_HANDLE *HandleBuffer;
	UINTN Index;

	//
	// Disconnect all
	//
	Status = gBS->LocateHandleBuffer(
			AllHandles,
			NULL,
			NULL,
			&HandleCount,
			&HandleBuffer
	);
	if (EFI_ERROR(Status)) {
		return Status;
	}

	for (Index = 0; Index < HandleCount; Index++) {
		//Status =
		gBS->DisconnectController(HandleBuffer[Index], NULL, NULL);
	}

	if (HandleBuffer != NULL) {
		FreePool(HandleBuffer);
	}

	return EFI_SUCCESS;
}

EFI_STATUS ScanDeviceHandles(EFI_HANDLE ControllerHandle,
														 UINTN *HandleCount,
														 EFI_HANDLE **HandleBuffer,
														 UINT32 **HandleType) {
	EFI_STATUS Status;
	UINTN HandleIndex;
	EFI_GUID **ProtocolGuidArray;
	UINTN ArrayCount;
	UINTN ProtocolIndex;
	EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *OpenInfo;
	UINTN OpenInfoCount;
	UINTN OpenInfoIndex;
	UINTN ChildIndex;

	*HandleCount = 0;
	*HandleBuffer = NULL;
	*HandleType = NULL;

	//
	// Retrieve the list of all handles from the handle database
	//
	Status = gBS->LocateHandleBuffer(AllHandles, NULL, NULL, HandleCount, HandleBuffer);
	if (EFI_ERROR(Status)) goto Error;

	*HandleType = AllocatePool(*HandleCount * sizeof(**HandleType));
	if (*HandleType == NULL) goto Error;

	for (HandleIndex = 0; HandleIndex < *HandleCount; HandleIndex++) {
		(*HandleType)[HandleIndex] = EFI_HANDLE_TYPE_UNKNOWN;
		//
		// Retrieve the list of all the protocols on each handle
		//
		Status = gBS->ProtocolsPerHandle(
				(*HandleBuffer)[HandleIndex],
				&ProtocolGuidArray,
				&ArrayCount
		);
		if (!EFI_ERROR(Status)) {
			for (ProtocolIndex = 0; ProtocolIndex < ArrayCount; ProtocolIndex++) {

				if (CompareGuid(ProtocolGuidArray[ProtocolIndex], &gEfiLoadedImageProtocolGuid) == 0) {
					(*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_IMAGE_HANDLE;
				}

				if (CompareGuid(ProtocolGuidArray[ProtocolIndex], &gEfiDriverBindingProtocolGuid) == 0) {
					(*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_DRIVER_BINDING_HANDLE;
				}

//				if (CompareGuid(ProtocolGuidArray[ProtocolIndex], &gEfiDriverConfigurationProtocolGuid) == 0) {
//					(*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_DRIVER_CONFIGURATION_HANDLE;
//				}
//
//				if (CompareGuid(ProtocolGuidArray[ProtocolIndex], &gEfiDriverDiagnosticsProtocolGuid) == 0) {
//					(*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_DRIVER_DIAGNOSTICS_HANDLE;
//				}

				if (CompareGuid(ProtocolGuidArray[ProtocolIndex], &gEfiComponentName2ProtocolGuid) == 0) {
					(*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_COMPONENT_NAME_HANDLE;
				}

				if (CompareGuid(ProtocolGuidArray[ProtocolIndex], &gEfiComponentNameProtocolGuid) == 0) {
					(*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_COMPONENT_NAME_HANDLE;
				}

				if (CompareGuid(ProtocolGuidArray[ProtocolIndex], &gEfiDevicePathProtocolGuid) == 0) {
					(*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_DEVICE_HANDLE;
				}

				//
				// Retrieve the list of agents that have opened each protocol
				//
				Status = gBS->OpenProtocolInformation(
						(*HandleBuffer)[HandleIndex],
						ProtocolGuidArray[ProtocolIndex],
						&OpenInfo,
						&OpenInfoCount
				);
				if (!EFI_ERROR(Status)) {

					for (OpenInfoIndex = 0; OpenInfoIndex < OpenInfoCount; OpenInfoIndex++) {

						if (OpenInfo[OpenInfoIndex].ControllerHandle == ControllerHandle) {
							if ((OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) == EFI_OPEN_PROTOCOL_BY_DRIVER) {
								for (ChildIndex = 0; ChildIndex < *HandleCount; ChildIndex++) {
									if ((*HandleBuffer)[ChildIndex] == OpenInfo[OpenInfoIndex].AgentHandle) {
										(*HandleType)[ChildIndex] |= EFI_HANDLE_TYPE_DEVICE_DRIVER;
									}
								}
							}

							if ((OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) ==
									EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) {
								(*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_PARENT_HANDLE;
								for (ChildIndex = 0; ChildIndex < *HandleCount; ChildIndex++) {
									if ((*HandleBuffer)[ChildIndex] == OpenInfo[OpenInfoIndex].AgentHandle) {
										(*HandleType)[ChildIndex] |= EFI_HANDLE_TYPE_BUS_DRIVER;
									}
								}
							}
						}
					}

//MsgLog("ScanDeviceHandles FreePool(OpenInfo)\n");
					FreePool(OpenInfo);
//MsgLog("ScanDeviceHandles FreePool(OpenInfo) after\n");
				}
			}
//MsgLog("ScanDeviceHandles FreePool(ProtocolGuidArray)\n");

			FreePool(ProtocolGuidArray);
//MsgLog("ScanDeviceHandles FreePool(ProtocolGuidArray) after\n");
		}
	}

	return EFI_SUCCESS;

	Error:
	if (*HandleType != NULL) {
		FreePool(*HandleType);
	}

	if (*HandleBuffer != NULL) {
		FreePool(*HandleBuffer);
	}

	*HandleCount = 0;
	*HandleBuffer = NULL;
	*HandleType = NULL;

	return Status;
}


EFI_STATUS BdsLibConnectMostlyAllEfi() {
	EFI_STATUS Status;
	UINTN AllHandleCount = 0;
	EFI_HANDLE *AllHandleBuffer = NULL;
	UINTN Index;
	UINTN HandleCount = 0;
	EFI_HANDLE *HandleBuffer = NULL;
	UINT32 *HandleType = NULL;
	UINTN HandleIndex;
	BOOLEAN Parent;
	BOOLEAN Device;
	EFI_PCI_IO_PROTOCOL *PciIo = NULL;
	PCI_TYPE00 Pci;

	Status = gBS->LocateHandleBuffer(AllHandles, NULL, NULL, &AllHandleCount, &AllHandleBuffer);
	if (EFI_ERROR(Status))
		return Status;

	for (Index = 0; Index < AllHandleCount; Index++) {
		Status = ScanDeviceHandles(AllHandleBuffer[Index], &HandleCount, &HandleBuffer, &HandleType);

		if (EFI_ERROR(Status))
			goto Done;

		Device = true;

		if (HandleType[Index] & EFI_HANDLE_TYPE_DRIVER_BINDING_HANDLE)
			Device = false;
		if (HandleType[Index] & EFI_HANDLE_TYPE_IMAGE_HANDLE)
			Device = false;

		if (Device) {
			Parent = false;
			for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
				if (HandleType[HandleIndex] & EFI_HANDLE_TYPE_PARENT_HANDLE)
					Parent = true;
			}

			if (!Parent) {
				if (HandleType[Index] & EFI_HANDLE_TYPE_DEVICE_HANDLE) {
					Status = gBS->HandleProtocol(AllHandleBuffer[Index], &gEfiPciIoProtocolGuid, (void **) &PciIo);
					if (!EFI_ERROR(Status)) {
						Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint32, 0, sizeof(Pci) / sizeof(UINT32), &Pci);
						if (!EFI_ERROR(Status)) {
							if (IS_PCI_VGA(&Pci) == true) {
								gBS->DisconnectController(AllHandleBuffer[Index], NULL, NULL);
							}
						}
					}
					Status = gBS->ConnectController(AllHandleBuffer[Index], NULL, NULL, true);
				}
			}
		}

		FreePool(HandleBuffer);
		FreePool(HandleType);
	}

	Done:
	FreePool(AllHandleBuffer);
	return Status;
}


/**
  Connect the specific Usb device which match the short form device path,
  and whose bus is determined by Host Controller (Uhci or Ehci).

  @param  HostControllerPI      Uhci (0x00) or Ehci (0x20) or Both uhci and ehci
                                (0xFF)
  @param  RemainingDevicePath   a short-form device path that starts with the first
                                element  being a USB WWID or a USB Class device
                                path

  @return EFI_INVALID_PARAMETER  RemainingDevicePath is NULL pointer.
                                 RemainingDevicePath is not a USB device path.
                                 Invalid HostControllerPI type.
  @return EFI_SUCCESS            Success to connect USB device
  @return EFI_NOT_FOUND          Fail to find handle for USB controller to connect.

**/
EFI_STATUS
EFIAPI
BdsLibConnectUsbDevByShortFormDP(
		IN UINT8 HostControllerPI,
		IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath
) {
	EFI_STATUS Status;
	EFI_HANDLE *HandleArray;
	UINTN HandleArrayCount;
	UINTN Index;
	EFI_PCI_IO_PROTOCOL *PciIo;
	UINT8 Class[3];
	BOOLEAN AtLeastOneConnected;

	//
	// Check the passed in parameters
	//
	if (RemainingDevicePath == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	if ((DevicePathType (RemainingDevicePath) != MESSAGING_DEVICE_PATH) ||
			((DevicePathSubType (RemainingDevicePath) != MSG_USB_CLASS_DP)
			 && (DevicePathSubType (RemainingDevicePath) != MSG_USB_WWID_DP)
			)) {
		return EFI_INVALID_PARAMETER;
	}

	if (HostControllerPI != 0xFF &&
			HostControllerPI != 0x00 &&
			HostControllerPI != 0x10 &&
			HostControllerPI != 0x20 &&
			HostControllerPI != 0x30) {
		return EFI_INVALID_PARAMETER;
	}

	//
	// Find the usb host controller firstly, then connect with the remaining device path
	//
	AtLeastOneConnected = false;
	Status = gBS->LocateHandleBuffer(
			ByProtocol,
			&gEfiPciIoProtocolGuid,
			NULL,
			&HandleArrayCount,
			&HandleArray
	);
	if (!EFI_ERROR(Status)) {
		for (Index = 0; Index < HandleArrayCount; Index++) {
			Status = gBS->HandleProtocol(
					HandleArray[Index],
					&gEfiPciIoProtocolGuid,
					(void **) &PciIo
			);
			if (!EFI_ERROR(Status)) {
				//
				// Check whether the Pci device is the wanted usb host controller
				//
				Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint8, 0x09, 3, &Class);
				if (!EFI_ERROR(Status)) {
					if ((PCI_CLASS_SERIAL == Class[2]) &&
							(PCI_CLASS_SERIAL_USB == Class[1])) {
						if (HostControllerPI == Class[0] || HostControllerPI == 0xFF) {
							Status = gBS->ConnectController(
									HandleArray[Index],
									NULL,
									RemainingDevicePath,
									false
							);
							if (!EFI_ERROR(Status)) {
								AtLeastOneConnected = true;
							}
						}
					}
				}
			}
		}

		if (HandleArray != NULL) {
			FreePool(HandleArray);
		}

		if (AtLeastOneConnected) {
			return EFI_SUCCESS;
		}
	}

	return EFI_NOT_FOUND;
}


/**
  Connects all drivers to all controllers.
  This function make sure all the current system driver will manage
  the corresponding controllers if have. And at the same time, make
  sure all the system controllers have driver to manage it if have.

**/
EFI_STATUS EFIAPI BdsLibConnectAllDriversToAllControllers(VOID) {
	EFI_STATUS efi_status;

//	do {
//		//
//		// Connect All EFI 1.10 drivers following EFI 1.10 algorithm
//		//
		efi_status = BdsLibConnectAllEfi();
		if (EFI_ERROR(efi_status)) {
			perror(L"BdsLibConnectAllEfi failed: %r\n", efi_status);
		}
		efi_status = BdsLibConnectMostlyAllEfi();
		if (EFI_ERROR(efi_status)) {
			perror(L"BdsLibConnectMostlyAllEfi failed: %r\n", efi_status);
		}
//		//
//		// Check to see if it's possible to dispatch an more DXE drivers.
//		// The BdsLibConnectAllEfi () may have made new DXE drivers show up.
//		// If anything is Dispatched Status == EFI_SUCCESS and we will try
//		// the connect again.
//		//
//		Status = gDS->Dispatch();
//
//	} while (!EFI_ERROR(Status));
//
	return efi_status;
}
