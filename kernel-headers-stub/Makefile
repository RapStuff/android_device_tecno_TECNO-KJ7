VERSION = 5
PATCHLEVEL = 10

BIONIC_UAPI := $(abspath $(CURDIR)/../../../../bionic/libc/kernel/uapi)
TARBALL := $(abspath $(CURDIR)/../../TECNO-KJ7-kernel/kernel-headers.tar.gz)

.PHONY: headers_install
headers_install:
	mkdir -p $(O)
	gzip -dc $(TARBALL) | tar -x -C $(O)
	rm -rf $(O)/usr/include/asm
	cd $(O)/usr/include && find . -type f ! -path "./asm-generic/signal.h" | while read -r f; do \
		if [ -e "$(BIONIC_UAPI)/$$f" ]; then rm -f "$$f"; fi; \
	done
