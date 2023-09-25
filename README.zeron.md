# zerox-shim

# Boot Config File

- Path: `\zerox-boot.cfg` in EFI-Volume
- Format: UTF-16 (Optional UTF-16LE BOM)
- Used ONLY on local drives (not network booting)

Example:

```text
chain_load=EFI/Microsoft/bootmgfw.orig.efi
rec_message=Press F4 key for recovery...
rec_key=F4
rec_efi=.zeron-aaaa:\EFI\ZeronsoftN\recovery.efi
onetime_efi=.zeron-bbbb:\EFI\ZeronsoftN\hello.efi
onetime_data=something
```

## rec_efi, onetime_efi format

### ":" 으로 구분 될 때

Example:
```
rec_efi=.zeron-aaaa:\EFI\ZeronsoftN\recovery.efi
```

- 모든 volume 에서 `.zeron-aaaa` 파일을 찾고, 그 볼륨의 `\EFI\ZeronsoftN\recovery.efi` 을 실행함.

### ":" 없이 경로만 있을 때

Example:
```
rec_efi=\EFI\ZeronsoftN\recovery.efi
```

- 부팅 한 EFI 볼륨의 `\EFI\ZeronsoftN\recovery.efi` 을 실행함.


# One-time boot file

- Path: `\zerox-boot.onetime` in EFI-Volume
- Format: `1-byte file`

File Content:

- Enabled: hex: `01`
- Disabled: hex: `00`
