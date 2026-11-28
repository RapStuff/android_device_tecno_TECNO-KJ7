VERSION = 5
PATCHLEVEL = 10

.PHONY: headers_install
headers_install:
	mkdir -p $(O)
	gzip -dc $(abspath $(CURDIR)/../../TECNO-KJ7-kernel/kernel-headers.tar.gz) | tar -x -C $(O)
	rm -rf $(O)/usr/include/asm $(O)/usr/include/linux/sched/types.h
