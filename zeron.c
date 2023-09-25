#include "shim.h"
#include "console.h"
#include "zeron.h"

#define CONFIG_PARSE_STATE_IDLE 0
#define CONFIG_PARSE_STATE_KEY 1
#define CONFIG_PARSE_STATE_VALUE 2
#define CONFIG_MAX_KEY_SIZE 32
#define CONFIG_MAX_VALUE_SIZE 128

#define FILE_INFO_SIZE (512 * sizeof(CHAR16))
#define PATH_BUFFER_SIZE 256

static EFI_GUID ZEROX_GUID = {0x5f031ff4,0xa248,0x4cf9,{0x81, 0x0c, 0xde, 0x28, 0xfe, 0x3b }};
static CHAR8 NTFSMagic[] = { 'N', 'T', 'F', 'S', ' ', ' ', ' ', ' ' };
static CHAR8 FAT32Magic[] = { 'M', 'S', 'D', 'O', 'S', '5', '.', '0' };

struct ZERON_CONFIG {
	CHAR16 chain_load[CONFIG_MAX_VALUE_SIZE];
	CHAR16 rec_message[CONFIG_MAX_VALUE_SIZE];
	CHAR16 rec_key[CONFIG_MAX_VALUE_SIZE];
	CHAR16 rec_efi_volume[CONFIG_MAX_VALUE_SIZE];
	CHAR16 rec_efi_path[CONFIG_MAX_VALUE_SIZE];
	CHAR16 onetime_volume[CONFIG_MAX_VALUE_SIZE];
	CHAR16 onetime_path[CONFIG_MAX_VALUE_SIZE];
	int onetime_boot;
	CHAR16 onetime_data[CONFIG_MAX_VALUE_SIZE];
};

extern EFI_STATUS start_image_ex(EFI_HANDLE image_handle, EFI_HANDLE DeviceHandle, EFI_DEVICE_PATH *FilePath, void *data, int datasize);

static struct ZERON_CONFIG zeron_config;
static BOOLEAN drivers_initialized = FALSE;
static EFI_HANDLE ntfs_driver_handle = NULL;

static EFI_STATUS disconnect_blocking_drivers();
static EFI_STATUS load_ntfs_driver(EFI_HANDLE image_handle, EFI_LOADED_IMAGE *li);

static EFI_STATUS load_image(
	EFI_HANDLE device,
	void **data,
	int *datasize,
	CHAR16 *PathName // Absolute path
);

static EFI_STATUS read_local_file(
	EFI_HANDLE device,
	IN CHAR16 *ImagePath, // Absolute path
	void **data,
	int *datasize
);

static EFI_STATUS read_config(EFI_LOADED_IMAGE *shim_li);
static EFI_STATUS check_onetime_boot(EFI_LOADED_IMAGE *li);

static EFI_STATUS wait_for_press_recovery();

static EFI_STATUS store_arguments(INTN argc, CHAR16 **argv);
static EFI_STATUS boot_to_rec(EFI_HANDLE image_handle, EFI_LOADED_IMAGE *li);
static EFI_STATUS boot_to_onetime(EFI_HANDLE image_handle, EFI_LOADED_IMAGE *li);

EFI_STATUS store_arguments(INTN argc, CHAR16 **argv) {
	int i;
	int total_size = 0;
	CHAR16 *buffer, *current;
	EFI_STATUS efi_status;

	if (argc <= 0) {
		return EFI_SUCCESS;
	}

	for (i=0; i < argc; i++) {
		total_size += StrLen(argv[i]) * 2 + 2;
	}

	buffer = AllocateZeroPool(total_size);
	if (!buffer) {
		return EFI_OUT_OF_RESOURCES;
	}

	current = buffer;
	for (i=0; i < argc; i++) {
		StrCpy(current, argv[i]);
		current += StrLen(argv[i]);
		*(current++) = 0;
	}

	efi_status = LibSetVariable(L"ShimArguments", &ZEROX_GUID, total_size, buffer);

	FreePool(buffer);

	return efi_status;
}

EFI_STATUS EFIAPI ZeronMain(EFI_HANDLE image_handle)
{
	EFI_STATUS efi_status;
	EFI_LOADED_IMAGE *shim_li;
	INTN argc;
	CHAR16 **argv;

	argc = GetShellArgcArgv(image_handle, &argv);
	efi_status = store_arguments(argc, argv);
	if (EFI_ERROR(efi_status)) {
		perror(L"Unable to store arguments: %r\n", efi_status);
	}

	/*
	 * We need to refer to the loaded image protocol on the running
	 * binary in order to find our path
	 */
	efi_status = BS->HandleProtocol(image_handle, &EFI_LOADED_IMAGE_GUID,
	                                (void **)&shim_li);
	if (EFI_ERROR(efi_status)) {
		perror(L"Unable to init protocol\n");
		return efi_status;
	}

	efi_status = read_config(shim_li);
	if (efi_status != EFI_SUCCESS) {
		return efi_status;
	}

	efi_status = check_onetime_boot(shim_li);
	if (efi_status == EFI_SUCCESS && zeron_config.onetime_boot) {
		efi_status = boot_to_onetime(image_handle, shim_li);
		if (efi_status == EFI_SUCCESS) {
			return efi_status;
		}
	}

	if ((StrLen(zeron_config.rec_efi_path) > 0) && (StrLen(zeron_config.rec_key) > 0)) {
		efi_status = wait_for_press_recovery();
		if (efi_status == EFI_SUCCESS) {
			return boot_to_rec(image_handle, shim_li);
		}
	}

	return EFI_SUCCESS;
}
static CHAR16* GetDriverName(
	EFI_HANDLE *ImageHandle,
	CONST EFI_HANDLE DriverHandle
) {
	CHAR16 *DriverName;
	EFI_COMPONENT_NAME_PROTOCOL *ComponentName;
	EFI_COMPONENT_NAME2_PROTOCOL *ComponentName2;

	// 먼저 EFI_COMPONENT_NAME2 프로토콜 사용
	if ((gBS->OpenProtocol(DriverHandle, &gEfiComponentName2ProtocolGuid, (VOID**)&ComponentName2, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL) == EFI_SUCCESS) &&
	    (ComponentName2->GetDriverName(ComponentName2, (CHAR8*)"", &DriverName) == EFI_SUCCESS)) {
		return DriverName;
	}

	// 작동하지 않는 경우 EFI_COMPONENT_NAME 사용
	if ((gBS->OpenProtocol(DriverHandle, &gEfiComponentNameProtocolGuid, (VOID**)&ComponentName, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL) == EFI_SUCCESS) &&
	    (ComponentName->GetDriverName(ComponentName, (CHAR8*)"", &DriverName) == EFI_SUCCESS)) {
		return DriverName;
	}

	return L"(Unknown driver)";
}

static EFI_STATUS load_ntfs_driver(EFI_HANDLE image_handle, EFI_LOADED_IMAGE *shim_li) {
	EFI_STATUS efi_status;
	EFI_HANDLE driver_handle = NULL;
	EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
	CHAR16* drive_path = L"\\EFI\\ZeronsoftN\\driver" EFI_ARCH "\\ntfs.efi";
	EFI_DEVICE_PATH *device_path = FileDevicePath(shim_li->DeviceHandle, drive_path);
	if (device_path == NULL) {
		perror(L"load_ntfs_driver: '%s' not found\n", drive_path);
		return EFI_NOT_FOUND;
	}

	efi_status = gBS->LoadImage(
		FALSE,
		image_handle,
		device_path,
		NULL,
		0,
		&driver_handle
	);
	if (EFI_ERROR(efi_status)) {
		perror(L"load_ntfs_driver: '%s' load failed: %r\n", drive_path, efi_status);
		return efi_status;
	}

	efi_status = gBS->OpenProtocol(
		driver_handle,
		&gEfiLoadedImageProtocolGuid,
		(VOID**)&loaded_image,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_GET_PROTOCOL
	);
	if (EFI_ERROR(efi_status)){
		perror(L"Cannot access the driver interface: %r\n", efi_status);
		return efi_status;
	}

	if (loaded_image->ImageCodeType != EfiBootServicesCode) {
		efi_status = EFI_LOAD_ERROR;
		perror(L"'ntfs.efi' is not a Boot System Driver\n");
		return efi_status;
	}

	efi_status = gBS->StartImage(driver_handle, NULL, NULL);
	if (EFI_ERROR(efi_status)){
		perror(L"Could not start the ntfs driver : %r\n", efi_status);
		return efi_status;
	}

	dprint(L"Start the driver : %s\n", GetDriverName(image_handle, driver_handle));

	ntfs_driver_handle = driver_handle;

	return efi_status;
}

/*
 * Some UEFI firmwares (like HPQ EFI from HP notebooks) have DiskIo protocols
 * opened BY_DRIVER (by Partition driver in HP's case) even when no file system
 * is produced from this DiskIo. This then blocks our FS driver from connecting
 * and producing file systems.
 * To fix it we disconnect drivers that connected to DiskIo BY_DRIVER if this
 * is a partition volume and if those drivers did not produce file system.
 *
 * https://sourceforge.net/p/cloverefiboot/code/3294/tree/rEFIt_UEFI/refit/main.c#l1271
 */
static EFI_STATUS disconnect_blocking_drivers(EFI_HANDLE MainImageHandle) {
	EFI_STATUS Status;
	UINTN HandleCount = 0, Index, OpenInfoIndex, OpenInfoCount;
	EFI_HANDLE *Handles = NULL;
	CHAR16 *DevicePathString;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Volume;
	EFI_BLOCK_IO_PROTOCOL *block_io;
	EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *OpenInfo;

	// Get all DiskIo handles
	Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiDiskIoProtocolGuid, NULL, &HandleCount, &Handles);
	if (EFI_ERROR(Status) || (HandleCount == 0))
		return Status;

	// Check every DiskIo handle
	for (Index = 0; Index < HandleCount; Index++) {
		// If this is not partition - skip it.
		// This is then whole disk and DiskIo
		// should be opened here BY_DRIVER by Partition driver
		// to produce partition volumes.
		Status = gBS->OpenProtocol(Handles[Index], &gEfiBlockIoProtocolGuid, (VOID**)&block_io,
		                           MainImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);

		if (EFI_ERROR(Status))
			continue;
		if ((block_io->Media == NULL) || (!block_io->Media->LogicalPartition))
			continue;

		// If SimpleFileSystem is already produced - skip it, this is ok
		Status = gBS->OpenProtocol(Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID**)&Volume,
		                           MainImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (Status == EFI_SUCCESS)
			continue;

		DevicePathString = DevicePathToStr(DevicePathFromHandle(Handles[Index]));

		// If no SimpleFileSystem on this handle but DiskIo is opened BY_DRIVER
		// then disconnect this connection
		Status = gBS->OpenProtocolInformation(Handles[Index], &gEfiDiskIoProtocolGuid, &OpenInfo, &OpenInfoCount);
		if (EFI_ERROR(Status)) {
			perror(L"  Could not get DiskIo protocol for %s: %r", DevicePathString, Status);
			FreePool(DevicePathString);
			continue;
		}

		for (OpenInfoIndex = 0; OpenInfoIndex < OpenInfoCount; OpenInfoIndex++) {
			if ((OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) == EFI_OPEN_PROTOCOL_BY_DRIVER) {
				Status = gBS->DisconnectController(Handles[Index], OpenInfo[OpenInfoIndex].AgentHandle, NULL);
				if (EFI_ERROR(Status)) {
					perror(L"  Could not disconnect '%s' on %s",
					           GetDriverName(MainImageHandle, OpenInfo[OpenInfoIndex].AgentHandle), DevicePathString);
				} else {
					perror(L"  Disconnected '%s' on %s ",
					             GetDriverName(MainImageHandle, OpenInfo[OpenInfoIndex].AgentHandle), DevicePathString);
				}
			}
		}
		FreePool(DevicePathString);
		FreePool(OpenInfo);
	}
	FreePool(Handles);
	return EFI_SUCCESS;
}

static EFI_STATUS load_drivers(EFI_HANDLE image_handle, EFI_LOADED_IMAGE *li) {
	EFI_STATUS efi_status;

	if (drivers_initialized) {
		return EFI_SUCCESS;
	}
	drivers_initialized = TRUE;

	efi_status = disconnect_blocking_drivers(image_handle);
	efi_status = load_ntfs_driver(image_handle, li);

	if (EFI_ERROR(efi_status)) {
		perror(L"NTFS Driver Load Failed: %r\n", efi_status);
	}

	return efi_status;
}

static EFI_STATUS find_volume(EFI_HANDLE image_handle, EFI_HANDLE* found_handle, CHAR16* root_name) {
	EFI_STATUS efi_status;
	EFI_HANDLE *handles = NULL;
	UINTN handle_count = 0;
	UINT32 buffer_cap = 0;
	CHAR8* buffer = NULL;

	*found_handle = NULL;

	efi_status = gBS->LocateHandleBuffer(ByProtocol, &gEfiDiskIoProtocolGuid, NULL, &handle_count, &handles);
	if (EFI_ERROR(efi_status)){
		perror(L"Failed to list disks : %rx\n", efi_status);
		return efi_status;
	}

	dprint(L"handle_count: %d\n", handle_count);

	for(UINTN i = 0; i < handle_count; i++){
		EFI_BLOCK_IO_PROTOCOL *block_io;
		EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *volume;
		BOOLEAN is_fat32;
		BOOLEAN is_ntfs;
		EFI_FILE_HANDLE root = NULL;
		EFI_FILE_HANDLE target_handle = NULL;

		dprint(L"HANDLE[%d]\n", i);

		efi_status = gBS->OpenProtocol(handles[i], &gEfiBlockIoProtocolGuid, (VOID**)&block_io, image_handle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (EFI_ERROR(efi_status)){
			perror(L"> OpenProtocol Error : %r\n", efi_status);
			continue;
		}

		if (buffer_cap < block_io->Media->BlockSize) {
			if (buffer) {
				FreePool(buffer);
			}
			buffer = AllocatePool(block_io->Media->BlockSize);
			if (!buffer) {
				perror(L"> Allocate(%d) Error\n", block_io->Media->BlockSize);
				continue;
			}
		}

		efi_status = block_io->ReadBlocks(block_io, block_io->Media->MediaId, 0, block_io->Media->BlockSize, buffer);
		if (EFI_ERROR(efi_status)) {
			perror(L"> ReadBlock Error %r\n", efi_status);
			break;
		}

		is_fat32 = (CompareMem(&buffer[3], FAT32Magic, sizeof(FAT32Magic)) == 0);
		if (!is_fat32) {
			is_fat32 |= CompareMem(&buffer[3], "mkdosfs", 8) == 0;
			is_fat32 |= CompareMem(&buffer[3], "mkfs.fat", 8) == 0;
		}
		is_ntfs = (CompareMem(&buffer[3], NTFSMagic, sizeof(NTFSMagic)) == 0);

		if (!is_ntfs && !is_fat32) {
			dprint(L"> Not NTFS and FAT32\n");
			continue ;
		}

		efi_status = gBS->OpenProtocol(
			handles[i],
			&gEfiSimpleFileSystemProtocolGuid,
			(VOID**)&volume,
			image_handle,
			NULL,
			EFI_OPEN_PROTOCOL_TEST_PROTOCOL
		);
		if (efi_status == EFI_SUCCESS) {
			dprint(L"> loaded\n");
		} else if (efi_status == EFI_UNSUPPORTED && is_ntfs && ntfs_driver_handle) {
			EFI_HANDLE driver_handle_lists[2] = {
				ntfs_driver_handle,
				NULL,
			};
			dprint(L"> try connect to ntfs\n");

			efi_status = gBS->ConnectController(handles[i], driver_handle_lists, NULL, TRUE);
			if (EFI_ERROR(efi_status)) {
				perror(L"> ConnectController Error: %r\n", efi_status);
				continue ;
			}
		} else {
			perror(L"> OpenProtocol Error: %r\n", efi_status);
			continue ;
		}

		for (INTN retry = 0; retry < 3; retry++) {
			efi_status = gBS->OpenProtocol(handles[i], &gEfiSimpleFileSystemProtocolGuid, (VOID**)&volume, image_handle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
			if (!EFI_ERROR(efi_status)) {
				break;
			}
			dprint(L"Waiting 5 seconds before trying again ...");
			gBS->Stall(5 * 500000); // Microseconds
		}
		if (EFI_ERROR(efi_status)) {
			perror(L"> OpenProtocol Error: %r\n", efi_status);
			continue ;
		}

		efi_status = volume->OpenVolume(volume, &root);
		if (EFI_ERROR(efi_status) || (root == NULL)) {
			perror(L"> Could not open root directory: %r\n", efi_status);
			continue;
		}

		efi_status = root->Open(root, &target_handle, root_name, EFI_FILE_MODE_READ, 0);
		if (efi_status == EFI_SUCCESS) {
			root->Close(target_handle);
			*found_handle = handles[i];
			break ;
		}
	}

	if (buffer) {
		FreePool(buffer);
	}

	return efi_status;
}

static EFI_STATUS boot_to_rec(EFI_HANDLE image_handle, EFI_LOADED_IMAGE *li) {
	EFI_STATUS efi_status;
	EFI_HANDLE volume_handle = NULL;
	void* data;
	int datasize;

	load_drivers(image_handle, li);

	if (zeron_config.rec_efi_volume[0] != 0) {
		efi_status = find_volume(image_handle, &volume_handle,
		                         zeron_config.onetime_volume);
		if (EFI_ERROR(efi_status)) {
			perror(L"'%s' not found on any drive: %r\n", zeron_config.onetime_volume, efi_status);
			return efi_status;
		}
	} else {
		volume_handle = li->DeviceHandle;
	}
	EFI_DEVICE_PATH *device_path = FileDevicePath(NULL, zeron_config.rec_efi_path);
	if (!volume_handle) {
		volume_handle = li->DeviceHandle;
	}
	efi_status = load_image(volume_handle, &data, &datasize, zeron_config.rec_efi_path);
	if (EFI_ERROR(efi_status)) {
		return efi_status;
	}
	console_print(L"recovery boot to %s\n", zeron_config.rec_efi_path);
	return start_image_ex(image_handle, volume_handle, device_path, data, datasize);
}

static EFI_STATUS boot_to_onetime(EFI_HANDLE image_handle, EFI_LOADED_IMAGE *li) {
	EFI_STATUS efi_status;
	EFI_HANDLE volume_handle = NULL;
	void* data;
	int datasize;

	load_drivers(image_handle, li);

	if (zeron_config.onetime_volume[0] != 0) {
		efi_status = find_volume(image_handle, &volume_handle,
		                         zeron_config.onetime_volume);
		if (EFI_ERROR(efi_status)) {
			perror(L"'%s' not found on any drive: %r\n", zeron_config.onetime_volume, efi_status);
			return efi_status;
		}
	} else {
		volume_handle = li->DeviceHandle;
	}
	EFI_DEVICE_PATH *device_path = FileDevicePath(NULL, zeron_config.onetime_path);
	efi_status = load_image(volume_handle, &data, &datasize, zeron_config.onetime_path);
	if (EFI_ERROR(efi_status)) {
		return efi_status;
	}
	console_print(L"onetime boot to %s (%s)\n", zeron_config.onetime_path, DevicePathToStr(device_path));
	return start_image_ex(image_handle, volume_handle, device_path, data, datasize);
}

static UINT16 get_scan_code(IN CHAR16 *function_key)
{
	UINT16 scan_code;
	
	if (StrCaseCmp(function_key, L"F1") == 0) {
		scan_code = 0x000B;
	} else if (StrCaseCmp(function_key, L"F2") == 0) {
		scan_code = 0x000C;
	} else if (StrCaseCmp(function_key, L"F3") == 0) {
		scan_code = 0x000D;
	} else if (StrCaseCmp(function_key, L"F4") == 0) {
		scan_code = 0x000E;
	} else if (StrCaseCmp(function_key, L"F5") == 0) {
		scan_code = 0x000F;
	} else if (StrCaseCmp(function_key, L"F6") == 0) {
		scan_code = 0x0010;
	} else if (StrCaseCmp(function_key, L"F7") == 0) {
		scan_code = 0x0011;
	} else if (StrCaseCmp(function_key, L"F8") == 0) {
		scan_code = 0x0012;
	} else if (StrCaseCmp(function_key, L"F9") == 0) {
		scan_code = 0x0013;
	} else if (StrCaseCmp(function_key, L"F10") == 0) {
		scan_code = 0x0014;
	} else if (StrCaseCmp(function_key, L"F11") == 0) {
		scan_code = 0x0015;
	} else if (StrCaseCmp(function_key, L"F12") == 0) {
		scan_code = 0x0016;
	} else {
		scan_code = 0x0015;
	}

	return scan_code;
}

static EFI_STATUS wait_for_press_recovery()
{
	UINT64 wait_time = 5;
	UINT16 scan_code = get_scan_code(zeron_config.rec_key);
	
	EFI_STATUS efi_status;
	EFI_EVENT timer_event;
	EFI_EVENT wait_list[2];
	EFI_INPUT_KEY key;
	UINTN wait_index;

	efi_status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &timer_event);
	if (EFI_ERROR(efi_status)) {
		perror(L"CreateEvent Failed: %r\n", efi_status);
		return efi_status;
	}

	efi_status = gBS->SetTimer(timer_event, TimerRelative, wait_time * 10000000);
	if (EFI_ERROR(efi_status)) {
		perror(L"CreateEvent Failed: %r\n", efi_status);
		return efi_status;
	}

	wait_list[0] = gST->ConIn->WaitForKey;
	wait_list[1] = timer_event;

	console_print(L"%s\n", zeron_config.rec_message);

	do {
		efi_status = gBS->WaitForEvent(2, wait_list, &wait_index);
		if (!EFI_ERROR(efi_status) && wait_index == 1) {
			efi_status = EFI_TIMEOUT;
			dprint("wait timeout");
			gBS->CloseEvent(timer_event);
			break;
		}

		gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
		if (key.ScanCode == scan_code) {
			efi_status = EFI_SUCCESS;
			dprint("Pressed key : %d\n", key.ScanCode);
			break;
		}
	} while ((efi_status == EFI_TIMEOUT) || (key.ScanCode != scan_code));

	return efi_status;
}

static EFI_STATUS read_local_file(
	EFI_HANDLE device,
	IN CHAR16 *ImagePath,
	void **data,
	int *datasize
) {
	EFI_STATUS efi_status;

	if (findNetboot(device)) {
		return EFI_UNSUPPORTED;
	} else if (find_httpboot(device)) {
		return EFI_UNSUPPORTED;
	} else {
		/*
		 * Read the new executable off disk
		 */
		efi_status = load_image(device, data, datasize, ImagePath);
		if (EFI_ERROR(efi_status)) {
			perror(L"Failed to load image %s: %r\n", ImagePath, efi_status);
			PrintErrors();
			ClearErrors();
			return efi_status;
		}
	}

	if (*datasize < 0)
		efi_status = EFI_INVALID_PARAMETER;

	return efi_status;
}

static EFI_STATUS read_config(
	EFI_LOADED_IMAGE *shim_li
)
{
	EFI_STATUS status;
	CHAR16 *config_buf = NULL;
	int config_size = 0;
	CHAR16 *config_ptr;
	CHAR16 *config_end;

	int parse_state = CONFIG_PARSE_STATE_IDLE;
	int     key_len = 0;
	CHAR16  key_buf[CONFIG_MAX_KEY_SIZE];
	int     value_len = 0;
	CHAR16* value_ref = NULL;
	CHAR16  value_buf[CONFIG_MAX_VALUE_SIZE];

	SetMem(&zeron_config, sizeof(zeron_config), 0);

	status = read_local_file(shim_li->DeviceHandle, L"\\zerox-boot.cfg", (void**) &config_buf, &config_size);
	if (status != EFI_SUCCESS) {
		return status;
	}

	if (config_size < 1) {
		return EFI_UNSUPPORTED;
	}

	config_ptr = config_buf;
	config_end = config_buf + config_size;
	if (config_ptr[0] == 0xFEFF) {
		// UTF-16 BOM
		config_size--;
	}

	while (config_ptr < config_end && *config_ptr) {
		CHAR16 c = *config_ptr;

		switch (parse_state) {
		case CONFIG_PARSE_STATE_IDLE:
			if (c == L'#' || c == L' ' || c == L'\t' || c == '\r') {
				break;
			}
			parse_state = CONFIG_PARSE_STATE_KEY;
			key_len = 0;
			__attribute__ ((fallthrough));
		case CONFIG_PARSE_STATE_KEY:
			if (c == L'=') {
				// trim
				while ((key_len > 0) && (value_ref[key_len - 1] == L' ' || value_ref[key_len - 1] == L'\r')) {
					key_len--;
				}
				key_buf[key_len] = 0;

				if (StrCaseCmp(key_buf, L"chain_load") == 0) {
					value_ref = zeron_config.chain_load;
				} else if (StrCaseCmp(key_buf, L"rec_message") == 0) {
					value_ref = zeron_config.rec_message;
				} else if (StrCaseCmp(key_buf, L"rec_key") == 0) {
					value_ref = zeron_config.rec_key;
				} else if (StrCaseCmp(key_buf, L"onetime_data") == 0) {
					value_ref = zeron_config.onetime_data;
				} else {
					// include rec_efi, onetime_efi
					value_ref = value_buf;
				}
				value_len = 0;

				parse_state = CONFIG_PARSE_STATE_VALUE;
			} else if (key_len < (CONFIG_MAX_KEY_SIZE - 1)) {
				key_buf[key_len++] = c;
			}

			// ignore overflow
			break;

		case CONFIG_PARSE_STATE_VALUE:
			if (c == L'\n') {
				BOOLEAN is_rec_efi = StrCaseCmp(key_buf, L"rec_efi") == 0;
				BOOLEAN is_onetime_efi = StrCaseCmp(key_buf, L"onetime_efi") == 0;
				CHAR16* volume = NULL;
				CHAR16* path   = NULL;

				if (value_ref) {
					// trim
					while ((value_len > 0) && (value_ref[value_len - 1] == L' ' || value_ref[value_len - 1] == L'\r')) {
						value_len--;
					}
					value_ref[value_len] = 0;
				}

				if (is_rec_efi) {
					volume = zeron_config.rec_efi_volume;
					path = zeron_config.rec_efi_path;
				} else if (is_onetime_efi) {
					volume = zeron_config.onetime_volume;
					path = zeron_config.onetime_path;
				}

				// Parse volume and path
				if (is_rec_efi || is_onetime_efi) {
					int pos = 0;
					int colon_at = -1;
					int j;
					while (value_ref[pos]) {
						if (value_ref[pos] == L':') {
							colon_at = pos;
							break ;
						}
						pos++;
					}

					volume[0] = 0;
					path[0] = 0;
					pos = 0;
					j = 0;
					if (colon_at >= 0) {
						volume[j++] = L'\\';
					}
					while (value_ref[pos]) {
						if (pos < colon_at) {
							volume[j++] = value_ref[pos];
						} else if (pos == colon_at) {
							volume[j] = 0;
							j = 0;
						} else {
							path[j++] = value_ref[pos];
						}
						pos++;
					}
					path[j] = 0;
				}

				parse_state = CONFIG_PARSE_STATE_IDLE;
			} else if (value_ref && value_len < (CONFIG_MAX_VALUE_SIZE - 1)) {
				value_ref[value_len++] = c;
			}

			// ignore overflow
			break;
		}

		config_ptr++;
	}

	return EFI_SUCCESS;
}

/*
 * Read zerox-boot.onetime and remove flag
 */
static EFI_STATUS check_onetime_boot(
	EFI_LOADED_IMAGE *li
)
{
	CHAR16* PathName = L"\\zerox-boot.onetime";

	EFI_STATUS efi_status;
	EFI_HANDLE device;
	EFI_FILE_IO_INTERFACE *drive;
	EFI_FILE *root, *file;

	UINTN buffersize;
	UINT8 file_buffer[1];
	BOOLEAN writable = FALSE;

	device = li->DeviceHandle;

	dprint(L"attempting to load %s\n", PathName);
	/*
	 * Open the device
	 */
	efi_status = BS->HandleProtocol(device, &EFI_SIMPLE_FILE_SYSTEM_GUID,
	                                (void **) &drive);
	if (EFI_ERROR(efi_status)) {
		perror(L"check_onetime_boot: Failed to find fs: %r\n", efi_status);
		goto error;
	}

	efi_status = drive->OpenVolume(drive, &root);
	if (EFI_ERROR(efi_status)) {
		perror(L"check_onetime_boot: Failed to open fs: %r\n", efi_status);
		goto error;
	}

	/*
	 * And then open the file
	 */
	efi_status = root->Open(root, &file, PathName, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
	if (efi_status == EFI_SUCCESS) {
		writable = TRUE;
	} else if (efi_status == EFI_NOT_FOUND) {
		return efi_status;
	} else {
		perror(L"check_onetime_boot: Failed to open with writable %s - %r\n", PathName, efi_status);

		efi_status = root->Open(root, &file, PathName, EFI_FILE_MODE_READ, 0);
		if (efi_status == EFI_NOT_FOUND) {
			return efi_status;
		} else if (efi_status != EFI_SUCCESS) {
			perror(L"check_onetime_boot: Failed to open %s - %r\n", PathName, efi_status);
			goto error;
		}
	}

	buffersize = sizeof(file_buffer);

	/*
	 * Perform the actual read
	 */
	efi_status = file->Read(file, &buffersize, file_buffer);
	if (EFI_ERROR(efi_status)) {
		perror(L"check_onetime_boot: Unexpected return from initial read: %r, buffersize %x\n", efi_status, buffersize);
		goto error;
	}

	if (file_buffer[0] == 0x01) {
		zeron_config.onetime_boot = 1;

		if (writable) {
			file_buffer[0] = 0x00;
			efi_status = file->SetPosition(file, 0);
			if (EFI_ERROR(efi_status)) {
				perror(L"check_onetime_boot: Unexpected return from SetPosition: %r\n", efi_status);
				goto error;
			}

			buffersize = sizeof(file_buffer);
			efi_status = file->Write(file, &buffersize, file_buffer);
			if (EFI_ERROR(efi_status)) {
				perror(L"check_onetime_boot: Unexpected return from Write: %r\n", efi_status);
				goto error;
			}
		}
	}

	return EFI_SUCCESS;

error:
	return efi_status;
}

/*
 * Open the file and read it into a buffer
 */
static EFI_STATUS load_image(
	EFI_HANDLE device,
	void **data,
	int *datasize,
	CHAR16 *PathName
) {
	EFI_STATUS efi_status;
	EFI_FILE_INFO *fileinfo = NULL;
	EFI_FILE_IO_INTERFACE *drive;
	EFI_FILE *root, *file;
	UINTN buffersize = sizeof(EFI_FILE_INFO);

	dprint(L"attempting to load %s\n", PathName);
	/*
	 * Open the device
	 */
	efi_status = BS->HandleProtocol(device, &EFI_SIMPLE_FILE_SYSTEM_GUID,
	                                (void **) &drive);
	if (EFI_ERROR(efi_status)) {
		perror(L"load_image: Failed to find fs: %r\n", efi_status);
		goto error;
	}

	efi_status = drive->OpenVolume(drive, &root);
	if (EFI_ERROR(efi_status)) {
		perror(L"load_image: Failed to open fs: %r\n", efi_status);
		goto error;
	}

	/*
	 * And then open the file
	 */
	efi_status = root->Open(root, &file, PathName, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(efi_status)) {
		perror(L"load_image: Failed to open %s - %r\n", PathName, efi_status);
		goto error;
	}

	fileinfo = AllocatePool(buffersize);

	if (!fileinfo) {
		perror(L"load_image: Unable to allocate file info buffer\n");
		efi_status = EFI_OUT_OF_RESOURCES;
		goto error;
	}

	/*
	 * Find out how big the file is in order to allocate the storage
	 * buffer
	 */
	efi_status = file->GetInfo(file, &EFI_FILE_INFO_GUID, &buffersize,
	                           fileinfo);
	if (efi_status == EFI_BUFFER_TOO_SMALL) {
		FreePool(fileinfo);
		fileinfo = AllocatePool(buffersize);
		if (!fileinfo) {
			perror(L"load_image: Unable to allocate file info buffer\n");
			efi_status = EFI_OUT_OF_RESOURCES;
			goto error;
		}
		efi_status = file->GetInfo(file, &EFI_FILE_INFO_GUID,
		                           &buffersize, fileinfo);
	}

	if (EFI_ERROR(efi_status)) {
		perror(L"load_image: Unable to get file info: %r\n", efi_status);
		goto error;
	}

	buffersize = fileinfo->FileSize;
	*data = AllocatePool(buffersize);
	if (!*data) {
		perror(L"load_image: Unable to allocate file buffer\n");
		efi_status = EFI_OUT_OF_RESOURCES;
		goto error;
	}

	/*
	 * Perform the actual read
	 */
	efi_status = file->Read(file, &buffersize, *data);
	if (efi_status == EFI_BUFFER_TOO_SMALL) {
		FreePool(*data);
		*data = AllocatePool(buffersize);
		efi_status = file->Read(file, &buffersize, *data);
	}
	if (EFI_ERROR(efi_status)) {
		perror(L"Unexpected return from initial read: %r, buffersize %x\n",
		       efi_status, buffersize);
		goto error;
	}

	*datasize = buffersize;

	FreePool(fileinfo);

	return EFI_SUCCESS;
error:
	if (*data) {
		FreePool(*data);
		*data = NULL;
	}

	if (fileinfo)
		FreePool(fileinfo);
	return efi_status;
}
