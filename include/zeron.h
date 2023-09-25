#ifndef ZERON_H
#define ZERON_H

extern EFI_STATUS EFIAPI BdsLibConnectAllDriversToAllControllers(VOID);

extern EFI_STATUS EFIAPI ZeronMain(
	EFI_HANDLE image_handle
);

#endif /* ZERON_H */