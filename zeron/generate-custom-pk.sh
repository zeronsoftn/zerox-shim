virt-fw-vars \
 -i /usr/share/OVMF/OVMF_VARS_4M.fd \
 --enroll-generate "Test Platform Key" \
 --secure-boot \
 --add-db 7c46ca0a-85f0-489e-8342-25f249fa874e platform-secure-boot.crt \
 --output provisioned.vars.4m.fd
